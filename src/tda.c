#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <argp.h>

#define TMDS_CHANNEL_LEN        10
#define TMDS_CHUNK_PAD_LEN      2
#define TMDS_VALUE_MASK         0x3ff  /* 10 bit mask */

#define CTRLTOKEN_BLANK         0x354
#define CTRLTOKEN_HSYNC         0xab
#define CTRLTOKEN_VSYNC         0x154
#define CTRLTOKEN_VHSYNC        0x2ab

#define IMG_HSYNC_COLOR         0x90C3D4
#define IMG_VSYNC_COLOR         0xC390D4
#define IMG_VHSYNC_COLOR        0xD4A190
#define IMG_BLANK_COLOR         0xA1D490

#define IMG_PPM_HDR_FORMAT      "%02s %020d %020d 255\n"
#define IMG_PPM_HDR_MAGIC       "P6"

/* Prepare global variables for arg parser */
const char *argp_program_version = TDA_VERSION;
static char doc[] = "TMDS dump analyzer";
static char args_doc[] = "<tmds_dump>";

/* Prapare struct for holding parsed arguments */
struct arguments {
  char *tmds_dump_filename;
  char *rgb_dump_filename;
  int verbose;
  int quiet;
  int channel_info;
  int show_syncs;
  int align_rgb;
  int one_frame;
  int show_resolution;
  int show_resolution_blanks;
  int show_resolution_total;
  char *o_format;
};

static struct argp_option argp_options[] = {
  {"verbose",                   'v', 0,      0,
    "Produce verbose output" },
  {"quiet",                     'q', 0,      0,
    "Don't produce any output" },
  {"resolution",                'r', 0,      0,
    "Calculate and show the resolution of a single frame" },
  {"resolution-virt",           'R', 0,      0,
    "Calculate and show the resolution of a single frame including blanks" },
  {"resolution-total",          't', 0,      0,
    "Calculate and show the total resolution of chosen region" },
  {"channel-info",              'c', 0,      0,
    "Show count of control tokens on each channel" },
  {0,                            0,   0,      0,
    "Output writing options:"},
  {"format",                    'f', "FORMAT", 0,
    "Write the output file using specified FORMAT. " \
    "Available formats are: rgb/ppm." },
  {"out",                       'o', "FILE", 0,
    "Write decoded image data to FILE" },
  {"include-syncs",             's', 0,      0,
    "Include syncs visualization to the output file" },
  {"align",                     'a', 0,      0,
    "Align dumped data to first valid VSYNC" },
  {"one-frame",                 '1', 0,      0,
    "Try to extract and dump only one frame" },
  { 0 }
};

/* Define argument parser */
static error_t argp_parser(int key, char *arg, struct argp_state *state) {
  struct arguments *arguments = state->input;

  switch (key) {
  case 'v':
    arguments->verbose = 1;
    break;
  case 'q':
    arguments->quiet = 1;
    break;
  case 'c':
    arguments->channel_info = 1;
    break;
  case 'o':
    arguments->rgb_dump_filename = arg;
    break;
  case 's':
    arguments->show_syncs = 1;
    break;
  case 'a':
    arguments->align_rgb = 1;
    break;
  case '1':
    arguments->one_frame = 1;
    arguments->align_rgb = 1;
    break;
  case 'r':
    arguments->show_resolution = 1;
    break;
  case 'R':
    arguments->show_resolution_blanks = 1;
    break;
  case 't':
    arguments->show_resolution_total = 1;
    break;
  case 'f':
    arguments->o_format = arg;
    break;
  case ARGP_KEY_ARG:
    switch(state->arg_num) {
    case 0:
      arguments->tmds_dump_filename = arg;
      break;
    default:
      argp_usage(state);
    }
    break;
  case ARGP_KEY_END:
    if (state->arg_num < 1)
      argp_usage (state);
    break;
  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

/* Initialize the argp struct */
static struct argp argp = { argp_options, argp_parser, args_doc, doc, 0, 0, 0 };

/* Log related definitions */
#define LOG_INFO      0x1
#define LOG_ERROR     0x2
#define LOG_VERBOSE   0x4

uint8_t log_priority;

#define log(priority,format,args...) \
            do { \
              if (priority & log_priority) { \
              printf(format, ##args); } \
            } while(0);

/* Image analysis related definitions */
struct channel_stats {
  /* This struct contains number of control tokens in the channel */
  uint32_t blanks;
  uint32_t hsyncs;
  uint32_t vsyncs;
  uint32_t hvsyncs;
};

struct resolution {
  uint32_t x;
  uint32_t y;

  /* Helper fields for resolution calculation */
  uint32_t x_lckd;
  uint32_t last_token;
};

uint8_t tmds2rgb(uint16_t tmds)
{
  uint8_t rgb;
  uint8_t mid;
  int i;

  mid = (tmds & 0x200) ? ~tmds & 0xff : tmds & 0xff;

  rgb = mid & 0x1;
  for (i = 1; i < 8; i++) {
    if (tmds & 0x100) {
      rgb |= (((mid >> i) & 0x1) ^ ((mid >> (i - 1)) & 0x1)) << i;
    } else {
      rgb |= (((mid >> i) & 0x1) == ((mid >> (i - 1)) & 0x1)) << i;
    }
  }

  return rgb;
}

struct tmds_pixel {
  /* These are really 10b values */
  uint16_t d[3];
};

struct tmds_pixel parse_tmds_pixel(uint32_t data)
{
  struct tmds_pixel px;

  px.d[2] = data & TMDS_VALUE_MASK;
  data >>= TMDS_CHANNEL_LEN;
  px.d[1] = data & TMDS_VALUE_MASK;
  data >>= TMDS_CHANNEL_LEN;
  px.d[0] = data & TMDS_VALUE_MASK;

  return px;
}

uint8_t is_hsync(struct tmds_pixel px)
{
  int i;
  for (i = 0; i < 3; i++) {
    if (px.d[i] == CTRLTOKEN_HSYNC || px.d[i] == CTRLTOKEN_VHSYNC) {
      return 1;
    }
  }

  return 0;
}

uint8_t is_vsync(struct tmds_pixel px)
{
  int i;
  for (i = 0; i < 3; i++) {
    if (px.d[i] == CTRLTOKEN_VSYNC || px.d[i] == CTRLTOKEN_VHSYNC) {
      return 1;
    }
  }

  return 0;
}

uint8_t is_blank(struct tmds_pixel px)
{
  int i;
  for (i = 0; i < 3; i++) {
    if (px.d[i] == CTRLTOKEN_BLANK) {
      return 1;
    }
  }

  return 0;
}

uint8_t is_ctrl(struct tmds_pixel px)
{
  return is_hsync(px) || is_vsync(px) || is_blank(px);
}

int main(int argc, char *argv[])
{
  struct arguments args;
  int fd, fdo;
  uint32_t c, r;
  unsigned int i = 0, j;
  struct tmds_pixel px, ppx;
  uint32_t last_ctrl = 0, last_hsync = 0;
  struct channel_stats stats[3] = {};
  uint8_t data_aligned = 0, first_frame_ended = 0;
  struct resolution res = {}, resb = {}, rest = {};
  uint32_t rgb_px;
  char ppm_hdr[200] = {};

  /* Initialize arguments to zero */
  args.verbose = 0;
  args.quiet = 0;
  args.channel_info = 0;
  args.rgb_dump_filename = NULL;
  args.show_syncs = 0;
  args.align_rgb = 0;
  args.one_frame = 0;
  args.show_resolution = 0;
  args.show_resolution_blanks = 0;
  args.show_resolution_total = 0;
  args.o_format = "rgb";

  /* Parse program arguments */
  argp_parse(&argp, argc, argv, 0, 0, &args);

  /* Set logging options */
  if (args.verbose)
    log_priority |= LOG_VERBOSE;

  if (!args.quiet)
    log_priority |= LOG_INFO | LOG_ERROR;

  /* Check output format */
  if (strcasecmp(args.o_format, "RGB") && strcasecmp(args.o_format, "PPM")) {
    log(LOG_ERROR, "Format not supported: %s. Using RGB.\n", args.o_format);
  }

  fd = open(args.tmds_dump_filename, O_RDONLY);
  if (fd < 0) {
    log(LOG_ERROR, "Could not open %s file.\n", args.tmds_dump_filename);
    return -1;
  }

  if (args.rgb_dump_filename) {
    fdo = open(args.rgb_dump_filename,
               O_RDWR | O_CREAT | O_TRUNC,
               S_IRWXU | S_IRWXG);
    if (fdo < 0) {
      log(LOG_ERROR, "Could not open %s file.\n", args.rgb_dump_filename);
      return -1;
    }
    /* Reserve space for header for PPM format */
    if (strcasecmp(args.o_format, "ppm") == 0) {
      sprintf(ppm_hdr, IMG_PPM_HDR_FORMAT, IMG_PPM_HDR_MAGIC, 0, 0);
      write(fdo, ppm_hdr, strlen(ppm_hdr));
    }
  }

  /* Read first pixel */
  read(fd, &c, 4);
  px = parse_tmds_pixel(c);

  /* Loop over the rest of the dump */
  while(read(fd, &c, 4) != 0) {
    ppx = px; /* set previous pixel */
    px = parse_tmds_pixel(c);

    for (j = 0; j < 3; j++) {
      switch(px.d[j]) {
      case CTRLTOKEN_BLANK:
        log(LOG_VERBOSE, "D%d: Found BLANK @ %d!\n", j, i);
        stats[j].blanks++;
        break;
      case CTRLTOKEN_HSYNC:
        log(LOG_VERBOSE, "D%d: Found HSYNC @ %d!\n", j, i);
        stats[j].hsyncs++;
        break;
      case CTRLTOKEN_VSYNC:
        log(LOG_VERBOSE, "D%d: Found VSYNC @ %d!\n", j, i);
        stats[j].vsyncs++;
        break;
      case CTRLTOKEN_VHSYNC:
        log(LOG_VERBOSE, "D%d: Found VSYNC + HSYNC @ %d!\n", j, i);
        stats[j].hvsyncs++;
        break;
      }
    }

    /* Image width calculation */
    if (is_ctrl(px)) {
      if (!res.x_lckd && res.last_token && (i - res.last_token > 1)) {
        res.x = i - res.last_token - 1;
        res.x_lckd = 1;
      } else {
        res.last_token = i;
      }
    }
    if (is_hsync(px) && !is_hsync(ppx)) {
      if (!resb.x_lckd && resb.last_token && (i - resb.last_token > 1)) {
        resb.x = i - resb.last_token;
        resb.x_lckd = 1;
      } else {
        resb.last_token = i;
      }
    }

    /* Image height calculation */
    if (data_aligned && !first_frame_ended) {
      if (!is_ctrl(ppx) && is_ctrl(px)) {
          res.y++;
      }
      if (!is_hsync(ppx) && is_hsync(px)) {
        resb.y++;
      }
    }

    /* Check frame borders */
    if (!is_vsync(px) && is_vsync(ppx)) {
      if (data_aligned) {
        first_frame_ended = 1;
        if (args.one_frame) {
          break;
        }
      } else {
        data_aligned = 1;
      }
    }
    i++;

    if (args.align_rgb && !data_aligned) {
      continue;
    }

    if (! args.rgb_dump_filename)
      continue;

    if (!is_ctrl(px)) {
      rgb_px = tmds2rgb(px.d[0]);
      rgb_px |= tmds2rgb(px.d[1]) << 8;
      rgb_px |= tmds2rgb(px.d[2]) << 16;
      write(fdo, &rgb_px, 3);
    } else {
      if (!args.show_syncs)
        continue;

      if (is_hsync(px) && is_vsync(px)) {
        rgb_px = IMG_VHSYNC_COLOR;
      } else if (is_hsync(px)) {
        rgb_px = IMG_HSYNC_COLOR;
      } else if (is_vsync(px)) {
        rgb_px = IMG_VSYNC_COLOR;
      } else {
        rgb_px = IMG_BLANK_COLOR;
      }
      write(fdo, &rgb_px, 3);
    }
  }

  close(fd);
  close(fdo);

  /* Calculate total output resolution */
  rest.x = (args.show_syncs) ? resb.x : res.x;

  if (args.one_frame) {
    rest.y = (args.show_syncs) ? resb.y : res.y;
  } else {
    rest.y = i / (rest.x);
  }

  if (args.rgb_dump_filename && (strcasecmp(args.o_format, "ppm") == 0)) {
    /* Append header if using PPM format */
    if (args.rgb_dump_filename) {
      fdo = open(args.rgb_dump_filename, O_WRONLY, S_IRWXU | S_IRWXG);
      if (fdo < 0) {
        log(LOG_ERROR, "Could not re-open %s file.\n",
            args.rgb_dump_filename);
        return -1;
      }

      sprintf(ppm_hdr,
              IMG_PPM_HDR_FORMAT,
              IMG_PPM_HDR_MAGIC,
              rest.x,
              rest.y);

      lseek(fdo, 0, SEEK_SET);
      write(fdo, ppm_hdr, strlen(ppm_hdr));
    }
    close(fdo);
  }

  if (args.show_resolution)
    log(LOG_INFO, "Calculated frame resolution: %dx%d\n", res.x, res.y);

  if (args.show_resolution_blanks)
    log(LOG_INFO, "Calculated frame resolution with blanks: %dx%d\n",
        resb.x, resb.y);

  if (args.show_resolution_total)
    log(LOG_INFO, "Calculated total resolution of chosen region: %dx%d\n",
        rest.x, rest.y);

  if (args.channel_info)
    for (i = 0; i < 3; i++) {
      log(LOG_INFO, "(d%d) (b:%8d) (h:%8d) (v:%8d) (hv:%8d) (total: %8d)\n",
                     i,
                     stats[i].blanks,
                     stats[i].hsyncs,
                     stats[i].vsyncs,
                     stats[i].hvsyncs,
                     stats[i].blanks + stats[i].hsyncs +
                     stats[i].vsyncs + stats[i].hvsyncs);
    }

  return 0;
}
