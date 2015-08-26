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

/* Prepare global variables for arg parser */
const char *argp_program_version = TDA_VERSION;
static char doc[] = "TMDS dump analyzer";
static char args_doc[] = "<tmds_dump_in> <rgb_dump_out>";

/* Prapare struct for holding parsed arguments */
struct arguments {
  char *tmds_dump_filename;
  char *rgb_dump_filename;
  int verbose;
  int quiet;
  int channel_info;
};

static struct argp_option argp_options[] = {
  {"verbose",       'v', 0, 0, "Produce verbose output" },
  {"quiet",         'q', 0, 0, "Don't produce any output" },
  {"channel-info",  'c', 0, 0, "Show count of control tokens on each channel" },
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
  case ARGP_KEY_ARG:
    switch(state->arg_num) {
    case 0:
      arguments->tmds_dump_filename = arg;
      break;
    case 1:
      arguments->rgb_dump_filename = arg;
      break;
    default:
      argp_usage(state);
    }
    break;
  case ARGP_KEY_END:
    if (state->arg_num < 2)
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
  uint16_t d0;
  uint16_t d1;
  uint16_t d2;
};

struct tmds_pixel parse_tmds_pixel(uint32_t data)
{
  struct tmds_pixel px;

  px.d2 = data & TMDS_VALUE_MASK;
  data >>= TMDS_CHANNEL_LEN;
  px.d1 = data & TMDS_VALUE_MASK;
  data >>= TMDS_CHANNEL_LEN;
  px.d0 = data & TMDS_VALUE_MASK;

  return px;
}

int main(int argc, char *argv[])
{
  struct arguments args;
  int fd, fdo;
  uint32_t c, r;
  unsigned int i = 0;
  struct tmds_pixel px;
  uint8_t ctrl_found = 0x0;
  struct channel_stats stats[3] = {};

  uint32_t rgb_px;

  /* Initialize arguments to zero */
  args.verbose = 0;
  args.quiet = 0;
  args.channel_info = 0;

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

  fdo = open(args.rgb_dump_filename, O_RDWR | O_CREAT | O_TRUNC);
  if (fdo < 0) {
    log(LOG_ERROR, "Could not open %s file.\n", args.rgb_dump_filename);
    return -1;
  }

  while(read(fd, &c, 4) != 0) {
    px = parse_tmds_pixel(c);

    switch(px.d0) {
    case CTRLTOKEN_BLANK:
      log(LOG_VERBOSE, "D0: Found BLANK @ %d!\n", i);
      stats[0].blanks++;
      ctrl_found = 1;
      break;
    case CTRLTOKEN_HSYNC:
      log(LOG_VERBOSE, "D0: Found HSYNC @ %d!\n", i);
      stats[0].hsyncs++;
      ctrl_found = 1;
      break;
    case CTRLTOKEN_VSYNC:
      log(LOG_VERBOSE, "D0: Found VSYNC @ %d!\n", i);
      stats[0].vsyncs++;
      ctrl_found = 1;
      break;
    case CTRLTOKEN_VHSYNC:
      log(LOG_VERBOSE, "D0: Found VSYNC + HSYNC @ %d!\n", i);
      stats[0].hvsyncs++;
      ctrl_found = 1;
      break;
    }

    switch(px.d1) {
    case CTRLTOKEN_BLANK:
      log(LOG_VERBOSE, "D1: Found BLANK @ %d!\n", i);
      stats[1].blanks++;
      ctrl_found = 1;
      break;
    case CTRLTOKEN_HSYNC:
      log(LOG_VERBOSE, "D1: Found HSYNC @ %d!\n", i);
      stats[1].hsyncs++;
      ctrl_found = 1;
      break;
    case CTRLTOKEN_VSYNC:
      log(LOG_VERBOSE, "D1: Found VSYNC @ %d!\n", i);
      stats[1].vsyncs++;
      ctrl_found = 1;
      break;
    case CTRLTOKEN_VHSYNC:
      log(LOG_VERBOSE, "D1: Found VSYNC + HSYNC @ %d!\n", i);
      stats[1].hvsyncs++;
      ctrl_found = 1;
      break;
    }

    switch(px.d2) {
    case CTRLTOKEN_BLANK:
      log(LOG_VERBOSE, "D2: Found BLANK @ %d!\n", i);
      stats[2].blanks++;
      ctrl_found = 1;
      break;
    case CTRLTOKEN_HSYNC:
      log(LOG_VERBOSE, "D2: Found HSYNC @ %d!\n", i);
      stats[2].hsyncs++;
      ctrl_found = 1;
      break;
    case CTRLTOKEN_VSYNC:
      log(LOG_VERBOSE, "D2: Found VSYNC @ %d!\n", i);
      stats[2].vsyncs++;
      ctrl_found = 1;
      break;
    case CTRLTOKEN_VHSYNC:
      log(LOG_VERBOSE, "D2: Found VSYNC + HSYNC @ %d!\n", i);
      stats[2].hvsyncs++;
      ctrl_found = 1;
      break;
    }

    if (!ctrl_found) {
      rgb_px = tmds2rgb(px.d0);
      rgb_px |= tmds2rgb(px.d1) << 8;
      rgb_px |= tmds2rgb(px.d2) << 16;
      write(fdo, &rgb_px, 4);
    } else {
      ctrl_found = 0;
    }

    i++;
  }

  close(fd);
  close(fdo);

  if (!args.channel_info)
    return 0;

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
