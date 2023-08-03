// Test EOS events
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <camlib.h>
#include <ptp.h>

struct PtpRuntime r;

void print_bytes(uint8_t *bytes, int n) {
	for (int i = 0; i < n; i++) {
		if (bytes[i] > 31 && bytes[i] < 128) {
			printf("'%c' ", bytes[i]);
		} else {
			printf("%02X ", bytes[i]);
		}
	}

	puts("");
}

void test();

int main() {
	ptp_generic_init(&r);

	if (ptp_device_init(&r)) {
		puts("Device connection error");
		return 0;
	}

	ptp_open_session(&r);
	
	struct PtpDeviceInfo di;

	ptp_get_device_info(&r, &di);

	char temp[4096];
	ptp_device_info_json(&di, temp, sizeof(temp));
	printf("%s\n", temp);

	int rc = ptp_recieve_int((char *)r.data, 100);
	printf("Int: %d\n", rc);

#if 0 // if EOS
	ptp_eos_set_remote_mode(&r, 1);
	ptp_eos_set_event_mode(&r, 1);

	while (1) {
		ptp_eos_get_event(&r);
		ptp_dump(&r);

		char buffer[50000];
		ptp_eos_events_json(&r, buffer, 50000);
		puts(buffer);

		usleep(1000 * 1000);
	}
#endif

	ptp_device_close(&r);

	free(r.data);
	return 0;
}

