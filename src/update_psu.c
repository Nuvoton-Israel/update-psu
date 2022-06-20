#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#define MAX_I2C_BUS	26
#define MAX_I2C_ADDR	0x7F
static int delay_scale = 0;
static int enter_prog_delay;
static int exit_prog_delay;
static int read_delay;
static int write_delay;
static int setpage_delay;

static char checksum(char *data, int len, char extra)
{
	char sum = 0;
	int i;

	for (i = 0; i < len; i++)
		sum += data[i];
	sum += extra;

	return (~sum + 1);
}

static int hexstr_to_int(char *hex, int len)
{
	char buf[33];

	if (len > 64)
		return -1;
	snprintf(buf, len + 1, "%s", hex);

	return strtol(buf, NULL, 16);
}

static int read_fw_info(int i2cdev, int address, unsigned char image)
{
	struct i2c_rdwr_ioctl_data i2c_rdwr;
	struct i2c_msg i2cmsg[2];
	unsigned char buf[32];
	int i, ret;

	buf[0] = 0xEF;
	buf[1] = 0x1;
	buf[2] = image;

        i2cmsg[0].addr = address;
        i2cmsg[0].flags = 0x00; // write
        i2cmsg[0].len = 3;
        i2cmsg[0].buf = buf;

        i2cmsg[1].addr = address;
        i2cmsg[1].flags = I2C_M_RD; // read
        i2cmsg[1].len = 11;
        i2cmsg[1].buf = buf;

	i2c_rdwr.msgs = i2cmsg;
	i2c_rdwr.nmsgs = 2;
	ret = ioctl(i2cdev, I2C_RDWR, &i2c_rdwr);
	if (ret < 0) {
		printf("%s: i2c err ret = %d\n", __func__, ret);
		return -1;
	}
	printf("%s:", __func__);
	for (i = 0; i < 11; i ++)
		printf("0x%02x ", buf[i]);
	printf("\n");

	return 0;
}

static unsigned char get_status(int i2cdev, int address)
{
	struct i2c_rdwr_ioctl_data i2c_rdwr;
	struct i2c_msg i2cmsg[2];
	unsigned char buf[32];
	int i, ret;

	buf[0] = 0xFC;

        i2cmsg[0].addr = address;
        i2cmsg[0].flags = 0x00;  // write
        i2cmsg[0].len = 1;
        i2cmsg[0].buf = buf;

        i2cmsg[1].addr = address;
        i2cmsg[1].flags = I2C_M_RD; // read
        i2cmsg[1].len = 2;
        i2cmsg[1].buf = buf;

	i2c_rdwr.msgs = i2cmsg;
	i2c_rdwr.nmsgs = 2;
	ret = ioctl(i2cdev, I2C_RDWR, &i2c_rdwr);
	if (ret < 0) {
		printf("%s: i2c err ret = %d\n", __func__, ret);
		return -1;
	}
	printf("get_status: 0x%02x\n", buf[0]);

	return buf[0];
}

static int get_delay(int i2cdev, int address, unsigned char category)
{
	struct i2c_rdwr_ioctl_data i2c_rdwr;
	struct i2c_msg i2cmsg[2];
	unsigned char buf[32];
	int i, ret;

	buf[0] = 0xF2;
	buf[1] = 0x1;
	buf[2] = category;

        i2cmsg[0].addr = address;
        i2cmsg[0].flags = 0x00; // write
        i2cmsg[0].len = 3;
        i2cmsg[0].buf = buf;

        i2cmsg[1].addr = address;
        i2cmsg[1].flags = I2C_M_RD; // read
        i2cmsg[1].len = 4;
        i2cmsg[1].buf = buf;

	i2c_rdwr.msgs = i2cmsg;
	i2c_rdwr.nmsgs = 2;
	ret = ioctl(i2cdev, I2C_RDWR, &i2c_rdwr);
	if (ret < 0) {
		printf("%s: i2c err ret = %d\n", __func__, ret);
		return -1;
	}
	if (buf[0] != 3) {
		printf("%s: error count = %d\n", __func__, buf[0]);
		return 0;
	}

	return (buf[1] << 16 | buf[2] << 8 | buf[3]);
}

static int write_data(int i2cdev, int address, char *hexdata, int offset, int data_len)
{
	struct i2c_rdwr_ioctl_data i2c_rdwr;
	struct i2c_msg i2cmsg[2];
	char buf[32];
	int i, ret, max_retry = 3;

	if ((data_len + 4) > (int)sizeof(buf))
		return -1;

retry:
	buf[0] = 0xFB;
	buf[1] = (char)(offset >> 8);
	buf[2] = (char)(offset & 0xFF);
	for (i = 0; i < data_len; i++) {
		buf[i + 3] = hexstr_to_int(hexdata + i * 2, 2);
	}
	buf[data_len + 3] = checksum(buf, data_len + 3, address << 1);

        i2cmsg[0].addr = address;
        i2cmsg[0].flags = 0x00;
        i2cmsg[0].len = data_len + 4;
        i2cmsg[0].buf = buf;

        i2cmsg[1].addr = address;
        i2cmsg[1].flags = I2C_M_RD;
        i2cmsg[1].len = 1;
        i2cmsg[1].buf = buf;

	i2c_rdwr.msgs = i2cmsg;
	i2c_rdwr.nmsgs = 2;
	ret = ioctl(i2cdev, I2C_RDWR, &i2c_rdwr);
	if (ret < 0) {
		printf("%s: i2c err ret = %d\n", __func__, ret);
		return -1;
	}
	if (buf[0] != 0x31) {
		printf("error on writing @0x%x, RTN=0x%02x\n", offset, buf[0]);
		get_status(i2cdev, address);
		if (max_retry--)
			goto retry;
		return -1;
	}
	usleep(delay_scale * write_delay);

	return 0;
}

static int get_rom_page(int i2cdev, int address)
{
	struct i2c_rdwr_ioctl_data i2c_rdwr;
	struct i2c_msg i2cmsg[2];
	unsigned char buf[2];
	int i, ret;

	buf[0] = 0xF8;

        i2cmsg[0].addr = address;
        i2cmsg[0].flags = 0x00; // write
        i2cmsg[0].len = 1;
        i2cmsg[0].buf = buf;

        i2cmsg[1].addr = address;
        i2cmsg[1].flags = I2C_M_RD; // read
        i2cmsg[1].len = 2;
        i2cmsg[1].buf = buf;

	i2c_rdwr.msgs = i2cmsg;
	i2c_rdwr.nmsgs = 2;
	ret = ioctl(i2cdev, I2C_RDWR, &i2c_rdwr);
	if (ret < 0) {
		printf("%s: i2c err ret = %d\n", __func__, ret);
		return -1;
	}

	printf("current page: %d\n", buf[0]);
	return buf[0];
}

static int set_rom_page(int i2cdev, int address, unsigned char page)
{
	struct i2c_rdwr_ioctl_data i2c_rdwr;
	struct i2c_msg i2cmsg;
	unsigned char buf[2];
	int i, ret;

	printf("%s: %d\n", __func__, page);
	buf[0] = 0xF8;
	buf[1] = page;
	buf[2] = ~((address << 1) + 0xF8 + page) + 1;

        i2cmsg.addr = address;
        i2cmsg.flags = 0x00; // write
        i2cmsg.len = 3;
        i2cmsg.buf = buf;

	i2c_rdwr.msgs = &i2cmsg;
	i2c_rdwr.nmsgs = 1;
	ret = ioctl(i2cdev, I2C_RDWR, &i2c_rdwr);
	if (ret < 0) {
		printf("%s: i2c err ret = %d\n", __func__, ret);
		return -1;
	}
	usleep(delay_scale * setpage_delay);

	return 0;
}

static int enter_programming_mode(int i2cdev, int address, unsigned char mode)
{
	struct i2c_rdwr_ioctl_data i2c_rdwr;
	struct i2c_msg i2cmsg;
	unsigned char buf[2];
	int ret;

	printf("%s: 0x%x\n", __func__, mode);
	buf[0] = 0xF0;
	buf[1] = mode;

        i2cmsg.addr = address;
        i2cmsg.flags = 0x00;
        i2cmsg.len = 2;
        i2cmsg.buf = buf;

	i2c_rdwr.msgs = &i2cmsg;
	i2c_rdwr.nmsgs = 1;
	ret = ioctl(i2cdev, I2C_RDWR, &i2c_rdwr);
	if (ret < 0) {
		printf("%s: i2c err ret = %d\n", __func__, ret);
		return -1;
	}
	usleep(delay_scale * enter_prog_delay);

	return 0;
}

static int exit_programming_mode(int i2cdev, int address, unsigned char mode)
{
	struct i2c_rdwr_ioctl_data i2c_rdwr;
	struct i2c_msg i2cmsg;
	unsigned char buf[3];
	int ret;

	printf("%s: 0x%x\n", __func__, mode);
	buf[0] = 0xF1;
	buf[1] = mode;
	buf[2] = ~((address << 1) + 0xF1 + mode) + 1;

        i2cmsg.addr = address;
        i2cmsg.flags = 0x00;
        i2cmsg.len = 3;
        i2cmsg.buf = buf;

	i2c_rdwr.msgs = &i2cmsg;
	i2c_rdwr.nmsgs = 1;
	ret = ioctl(i2cdev, I2C_RDWR, &i2c_rdwr);
	if (ret < 0) {
		printf("%s: i2c err ret = %d\n", __func__, ret);
		return -1;
	}
	usleep(delay_scale * exit_prog_delay);

	return 0;
}

static int update_firmware(int i2cdev, int address, char *fw_path)
{
	FILE *f;
	int nread;
	char *line = NULL;
	size_t len = 0;
	unsigned short record_type, offset, data_len, page;
	int ret;
	int program = 0;

	f= fopen(fw_path, "r");
	if (!f) {
		printf("fail to open %s\n", fw_path);
		return -1;
	}

	// update inactive image
	if (enter_programming_mode(i2cdev, address, 1) != 0) {
		fclose(f);
		return -1;
	}

	while ((nread = getline(&line, &len, f)) != -1) {
		if (line[0] != ':')
			continue;
		if (nread < 12)
			continue;
		data_len =  hexstr_to_int(line + 1, 2);
		if (nread < data_len + 12) {
			printf("nread = %d\n", nread);
			fwrite(line, nread, 1, stdout);
			ret = -1;
			break;
		}
		offset = hexstr_to_int(line + 3, 4);
		record_type = hexstr_to_int(line + 7, 2);
		if (record_type == 4) {
			page = hexstr_to_int(line + 9, 4);
			ret = set_rom_page(i2cdev, address, page);
			if (ret != 0)
				break;
		} else if (record_type == 0) {
			ret = write_data(i2cdev, address, line + 9, offset, data_len);
			if (ret != 0)
				break;
			program = 1;
		} else if (record_type == 1) {
			// exit programming
			break;
		}
	}

	if (!ret && program) {
		// switch to new image
		ret = exit_programming_mode(i2cdev, address, 0x31);
	} else {
		// exit program mode only
		ret = exit_programming_mode(i2cdev, address, 0xFA);
	}

	fclose(f);

	return ret;
}

int main(int argc, char *argv[])
{
	int i2cdev;
	unsigned int i2cbus, address;
	char devpath[20];
	int len;

	if (argc != 4) {
		fprintf(stderr, "Usage: %s ${bus_num} 0x${slave_addr} ${fw_path}\n", argv[0]);
		exit(1);
	}

	i2cbus = atoi(argv[1]);
	if (i2cbus > MAX_I2C_BUS)
		exit(1);

	address = strtoul(argv[2], NULL, 16);
	if (address > MAX_I2C_ADDR)
		exit(1);

	printf("bus %d addr 0x%x\n", i2cbus, address);

	len = snprintf(devpath, sizeof(devpath), "/dev/i2c-%d", i2cbus);
	if (len >= sizeof(devpath)) {
		fprintf(stderr, "%s: path truncated\n", devpath);
		exit(1);
	}
	i2cdev = open(devpath, O_RDWR);
	if (i2cdev < 0) {
		fprintf(stderr, "error open /dev/i2c-%d\n", i2cbus);
		exit(1);
	}

	delay_scale = get_delay(i2cdev, address, 0);
	if (delay_scale == -1) {
		fprintf(stderr, "error get base delay\n");
		close(i2cdev);
		exit(1);
	}
	enter_prog_delay = get_delay(i2cdev, address, 1);
	read_delay = get_delay(i2cdev, address, 2);
	write_delay = get_delay(i2cdev, address, 3);
	setpage_delay = get_delay(i2cdev, address, 4);
	exit_prog_delay = get_delay(i2cdev, address, 5);
#ifdef DEBUG
	printf("delay sacaling factor = %dus\n", delay_scale);
	printf("enter_prog delay = %d\n", enter_prog_delay);
	printf("exit_prog delay = %d\n", exit_prog_delay);
	printf("DataRead delay = %d\n", read_delay);
	printf("DataWrite delay = %d\n", write_delay);
	printf("SetPage delay = %d\n", setpage_delay);
	read_fw_info(i2cdev, address, 0xA);
	read_fw_info(i2cdev, address, 0xB);
#endif
	update_firmware(i2cdev, address, argv[3]);
	get_status(i2cdev, address);
	read_fw_info(i2cdev, address, 0xA);
	read_fw_info(i2cdev, address, 0xB);

	close(i2cdev);

	exit(0);
}
