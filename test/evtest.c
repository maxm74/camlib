#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <camlib.h>
#include <backend.h>
#include <ptp.h>
#include <operations.h>
#include <enum.h>

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
	r.data = malloc(10000);
	r.data_length = 10000;
	r.transaction = 0;
	r.session = 0;
	r.active_connection = 0;

	if (ptp_device_init(&r)) {
		puts("Device connection error");
		return 0;
	}

	ptp_open_session(&r);
	
	struct PtpDeviceInfo di;
	ptp_get_device_info(&r, &di);

	ptp_eos_set_remote_mode(&r, 1);
	ptp_eos_set_event_mode(&r, 1);

	//ptp_eos_get_prop_value(&r, 0xD1a6);

	ptp_eos_get_event(&r);
	//ptp_dump(&r);

	char buffer[50000];
	ptp_eos_events_json(&r, buffer, 50000);
	puts(buffer);

	ptp_device_close(&r);

	free(r.data);
	return 0;
}

