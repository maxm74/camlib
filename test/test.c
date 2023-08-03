// Scan filesystem
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <camlib.h>

int main() {
	struct PtpRuntime r;
	ptp_generic_init(&r);

	struct PtpDeviceInfo di;

	if (ptp_device_init(&r)) {
		puts("Device connection error");
		return 1;
	}

	ptp_open_session(&r);

	struct UintArray *arr;
	int rc = ptp_get_storage_ids(&r, &arr);
	if (rc) return rc;
	int id = arr->data[0];

	rc = ptp_get_object_handles(&r, id, 0, 0, &arr);
	if (rc) return rc;
	arr = ptp_dup_uint_array(arr);

	for (int i = 0; i < arr->length; i++) {
		struct PtpObjectInfo oi;
		rc = ptp_get_object_info(&r, arr->data[i], &oi);
		if (rc) return 1;

		printf("Filename: %s\n", oi.filename);
		printf("File size: %u\n", oi.compressed_size);
	}

	free(arr);

	rc = ptp_close_session(&r);
	if (rc) return 1;
	ptp_device_close(&r);
	ptp_generic_close(&r);
	return 0;
}
