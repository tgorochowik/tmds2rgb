#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#define TMDS_CHANNEL_LEN        10
#define TMDS_CHUNK_PAD_LEN      2
#define TMDS_VALUE_MASK         0x3ff  /* 10 bit mask */

#define CTRLTOKEN_BLANK         0x354
#define CTRLTOKEN_HSYNC         0xab
#define CTRLTOKEN_VSYNC         0x154
#define CTRLTOKEN_VHSYNC        0x2ab

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

int main(int argc, const char *argv[])
{
  int fd, fdo;
  uint32_t c, r;
  unsigned int i = 0;
  struct tmds_pixel px;
  uint8_t ctrl_found = 0x0;

  uint32_t rgb_px;

  if (argc < 3) {
    printf("Usage: %s <tmds_dump_in> <rgb_dump_out>\n", argv[0]);
    return -1;
  }

  fd = open(argv[1], O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "Could not open %s file.\n", argv[1]);
    return -1;
  }

  fdo = open(argv[2], O_RDWR | O_CREAT | O_TRUNC);
  if (fdo < 0) {
    fprintf(stderr, "Could not open %s file.\n", argv[2]);
    return -1;
  }

  while(read(fd, &c, 4) != 0) {
    px = parse_tmds_pixel(c);

    switch(px.d0) {
    case CTRLTOKEN_BLANK:
      /* Do not print blanks for now - too many of them */
      break;
    case CTRLTOKEN_HSYNC:
      printf("D0: Found HSYNC @ %d!\n", i);
      ctrl_found = 1;
      break;
    case CTRLTOKEN_VSYNC:
      printf("D0: Found VSYNC @ %d!\n", i);
      ctrl_found = 1;
      break;
    case CTRLTOKEN_VHSYNC:
      printf("D0: Found VSYNC + HSYNC @ %d!\n", i);
      ctrl_found = 1;
      break;
    }

    switch(px.d1) {
    case CTRLTOKEN_BLANK:
      /* Do not print blanks for now - too many of them */
      ctrl_found = 1;
      break;
    case CTRLTOKEN_HSYNC:
      printf("D1: Found HSYNC @ %d!\n", i);
      ctrl_found = 1;
      break;
    case CTRLTOKEN_VSYNC:
      printf("D1: Found VSYNC @ %d!\n", i);
      ctrl_found = 1;
      break;
    case CTRLTOKEN_VHSYNC:
      printf("D1: Found VSYNC + HSYNC @ %d!\n", i);
      ctrl_found = 1;
      break;
    }

    switch(px.d2) {
    case CTRLTOKEN_BLANK:
      /* Do not print blanks for now - too many of them */
      ctrl_found = 1;
      break;
    case CTRLTOKEN_HSYNC:
      printf("D2: Found HSYNC @ %d!\n", i);
      ctrl_found = 1;
      break;
    case CTRLTOKEN_VSYNC:
      printf("D2: Found VSYNC @ %d!\n", i);
      ctrl_found = 1;
      break;
    case CTRLTOKEN_VHSYNC:
      printf("D2: Found VSYNC + HSYNC @ %d!\n", i);
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
  return 0;
}
