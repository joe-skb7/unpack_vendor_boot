/*
 * Links:
 *
 * [1] https://source.android.com/devices/bootloader/partitions/vendor-boot-partitions
 * [2] https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/master/include/bootimg/bootimg.h
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VENDOR_BOOT_MAGIC	"VNDRBOOT"
#define VENDOR_BOOT_MAGIC_SIZE	8
#define VENDOR_BOOT_ARGS_SIZE	2048
#define VENDOR_BOOT_NAME_SIZE	16
#define VENDOR_HEADER_SIZE	2112

#define VENDOR_RAMDISK		"vendor_ramdisk.img"
#define VENDOR_DTB		"vendor_dtb.img"

#define PAGES(size, page_size)	(((size) + (page_size) - 1) / (page_size))
#define ALIGN(size, page_size)	((PAGES(size, page_size)) * (page_size))

/*
 * The structure of the vendor boot image (introduced with version 3 and
 * required to be present when a v3 boot image is used) is as follows:
 *
 * +---------------------+
 * | vendor boot header  | o pages
 * +---------------------+
 * | vendor ramdisk      | p pages
 * +---------------------+
 * | dtb                 | q pages
 * +---------------------+
 * o = (2112 + page_size - 1) / page_size
 * p = (vendor_ramdisk_size + page_size - 1) / page_size
 * q = (dtb_size + page_size - 1) / page_size
 *
 * 0. all entities in the vendor boot image are page_size (determined by the
 *    vendor and specified in the vendor boot image header) aligned in flash
 * 1. vendor ramdisk, and DTB are required (size != 0)
 */
struct vendor_boot_img_hdr {
	uint8_t magic[VENDOR_BOOT_MAGIC_SIZE];
	uint32_t header_version;
	uint32_t page_size;		/* flash page size we assume */

	uint32_t kernel_addr;		/* physical load addr */
	uint32_t ramdisk_addr;		/* physical load addr */

	uint32_t vendor_ramdisk_size;	/* size in bytes */

	uint8_t cmdline[VENDOR_BOOT_ARGS_SIZE];

	uint32_t tags_addr;		/* physical addr for kernel tags */

	uint8_t name[VENDOR_BOOT_NAME_SIZE]; /* asciiz product name */
	uint32_t header_size;		/* size of vendor boot image header in
					 * bytes */
	uint32_t dtb_size;		/* size of dtb image */
	uint64_t dtb_addr;		/* physical load address */
};

static void print_usage(char *app)
{
	printf("Usage: %s <path to vendor_boot.img>\n", app);
}

static int parse_args(int argc, char *argv[], char **path)
{
	if (argc != 2) {
		fprintf(stderr, "Error: Invalid argument count\n");
		print_usage(argv[0]);
		return -1;
	}

	*path = argv[1];
	return 0;
}

static int file_read(void *ptr, size_t size, FILE *stream)
{
	size_t num;

	num = fread(ptr, size, 1, stream);
	if (num != 1) {
		fprintf(stderr, "Error: Can't read file\n");
		if (feof(stream))
			fprintf(stderr, "End of file reached\n");
		else if (ferror(stream))
			fprintf(stderr, "I/O error occurred\n");
		return -1;
	}

	return 0;
}

static int file_write(const void *ptr, size_t size, FILE *stream)
{
	size_t num;

	num = fwrite(ptr, size, 1, stream);
	if (num != 1) {
		fprintf(stderr, "Error: Can't write file\n");
		if (feof(stream))
			fprintf(stderr, "End of file reached\n");
		else if (ferror(stream))
			fprintf(stderr, "I/O error occurred\n");
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	char *img_path;
	FILE *f_vbi, *f_wr;
	struct vendor_boot_img_hdr hdr;
	int ret = EXIT_FAILURE;
	int err;
	long size_hdr, size_rd; /* bytes */
	uint8_t *buf;

	err = parse_args(argc, argv, &img_path);
	if (err)
		return EXIT_SUCCESS;

	f_vbi = fopen(img_path, "r");
	if (!f_vbi) {
		fprintf(stderr, "Error: Can't open %s file; reason: %s\n",
			img_path, strerror(errno));
		return EXIT_FAILURE;
	}

	printf("--> Reading header...\n");
	err = file_read(&hdr, sizeof(hdr), f_vbi);
	if (err)
		goto err1;

	if (strncmp((char *)hdr.magic, VENDOR_BOOT_MAGIC,
		    VENDOR_BOOT_MAGIC_SIZE)) {
		fprintf(stderr, "Error: Magic doesn't match! Magic = \'%s\'\n",
			hdr.magic);

		goto err1;
	}

	/* Skip header trailing bytes due to page alignment */
	size_hdr = ALIGN(VENDOR_HEADER_SIZE, hdr.page_size);
	err = fseek(f_vbi, size_hdr, SEEK_SET);
	if (err) {
		fprintf(stderr, "Error: Can't seek %s file; reason: %s\n",
			img_path, strerror(errno));
		goto err1;
	}

	/* Read ramdisk */
	printf("--> Reading ramdisk...\n");
	buf = malloc(hdr.vendor_ramdisk_size);
	err = file_read(buf, hdr.vendor_ramdisk_size, f_vbi);
	if (err)
		goto err2;
	f_wr = fopen(VENDOR_RAMDISK, "w");
	if (!f_wr) {
		fprintf(stderr, "Error: Can't open %s file; reason: %s\n",
			VENDOR_RAMDISK, strerror(errno));
		goto err2;
	}
	err = file_write(buf, hdr.vendor_ramdisk_size, f_wr);
	if (err)
		goto err3;
	fclose(f_wr);
	free(buf);

	/* Skip ramdisk */
	size_rd = ALIGN(hdr.vendor_ramdisk_size, hdr.page_size);
	err = fseek(f_vbi, size_hdr + size_rd, SEEK_SET);
	if (err) {
		fprintf(stderr, "Error: Can't seek %s file; reason: %s\n",
			img_path, strerror(errno));
		goto err1;
	}

	/* Read dtb */
	printf("--> Reading dtb...\n");
	buf = malloc(hdr.dtb_size);
	err = file_read(buf, hdr.dtb_size, f_vbi);
	if (err)
		goto err2;
	f_wr = fopen(VENDOR_DTB, "w");
	if (!f_wr) {
		fprintf(stderr, "Error: Can't open %s file; reason: %s\n",
			VENDOR_DTB, strerror(errno));
		goto err2;
	}
	err = file_write(buf, hdr.dtb_size, f_wr);
	if (err)
		goto err3;

	printf("cmdline:      \'%s\'\n", hdr.cmdline);
	printf("product name: \'%s\'\n", hdr.name);

	ret = EXIT_SUCCESS;

err3:
	fclose(f_wr);
err2:
	free(buf);
err1:
	fclose(f_vbi);
	return ret;
}
