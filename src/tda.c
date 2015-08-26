#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
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
};

static struct argp_option argp_options[] = {
  {"verbose",       'v', 0,      0, "Produce verbose output" },
  {"quiet",         'q', 0,      0, "Don't produce any output" },
  {"resolution",    'r', 0,      0,
    "Calculate and show the resolution of a single frame" },
  {"channel-info",  'c', 0,      0,
    "Show count of control tokens on each channel" },
  {0,               0,   0,      0, "Output writing options:"},
  {"out",           'o', "FILE", 0, "Write decoded RGB data to FILE" },
  {"include-syncs", 's', 0,      0,
    "Include syncs visualization to the RGB dump" },
  {"align",         'a', 0,      0, "Align dumped data to first valid VSYNC" },
  {"one-frame",     '1', 0,      0, "Try to extract and dump only one frame" },
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
  uint8_t ctrl_found = 0x0;
  uint32_t last_ctrl = 0;
  struct channel_stats stats[3] = {};
  uint8_t data_aligned = 0, first_frame_ended = 0;
  uint32_t res_x = 0, res_y = 0, res_x_lckd = 0;
  uint32_t rgb_px;

  /* Initialize arguments to zero */
  args.verbose = 0;
  args.quiet = 0;
  args.channel_info = 0;
  args.rgb_dump_filename = NULL;
  args.show_syncs = 0;
  args.align_rgb = 0;
  args.one_frame = 0;
  args.show_resolution = 0;

  /* Parse program arguments */
  argp_parse(&argp, argc, argv, 0, 0, &args);

  /* Set logging options */
  if (args.verbose)
    log_priority |= LOG_VERBOSE;

  if (!args.quiet)
    log_priority |= LOG_INFO | LOG_ERROR;

  fd = open(args.tmds_dump_filename, O_RDONLY);
  if (fd < 0) {
    log(LOG_ERROR, "Could not open %s file.\n", args.tmds_dump_filename);
    return -1;
  }

  if (args.rgb_dump_filename) {
    fdo = open(args.rgb_dump_filename, O_RDWR | O_CREAT | O_TRUNC);
    if (fdo < 0) {
      log(LOG_ERROR, "Could not open %s file.\n", args.rgb_dump_filename);
      return -1;
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
        ctrl_found = 1;
        break;
      case CTRLTOKEN_HSYNC:
        log(LOG_VERBOSE, "D%d: Found HSYNC @ %d!\n", j, i);
        stats[j].hsyncs++;
        ctrl_found = 1;
        break;
      case CTRLTOKEN_VSYNC:
        log(LOG_VERBOSE, "D%d: Found VSYNC @ %d!\n", j, i);
        stats[j].vsyncs++;
        ctrl_found = 1;
        break;
      case CTRLTOKEN_VHSYNC:
        log(LOG_VERBOSE, "D%d: Found VSYNC + HSYNC @ %d!\n", j, i);
        stats[j].hvsyncs++;
        ctrl_found = 1;
        break;
      }
    }

    /* Image width calculation */
    if (ctrl_found) {
      if (!res_x_lckd && last_ctrl && (i - last_ctrl > 1)) {
        res_x = i - last_ctrl - 1;
        res_x_lckd = 1;
      } else {
        last_ctrl = i;
      }
    }

    /* Image height calculation */
    if (!is_ctrl(ppx) && is_ctrl(px)) {
      if (data_aligned && !first_frame_ended)
        res_y++;
    }

    /* Check frame borders */
    if (ctrl_found && !is_vsync(px) && is_vsync(ppx)) {
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
      ctrl_found = 0;
      continue;
    }

    if (!ctrl_found && args.rgb_dump_filename) {
      rgb_px = tmds2rgb(px.d[0]);
      rgb_px |= tmds2rgb(px.d[1]) << 8;
      rgb_px |= tmds2rgb(px.d[2]) << 16;
      write(fdo, &rgb_px, 4);
    } else {
      ctrl_found = 0;
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
      write(fdo, &rgb_px, 4);
    }
  }

  close(fd);
  close(fdo);

  if(args.show_resolution)
    log(LOG_INFO, "Calculated image resolution: %dx%d\n", res_x, res_y);

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
