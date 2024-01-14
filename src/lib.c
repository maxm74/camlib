// Library frontend functions
// Copyright 2022 by Daniel C (https://github.com/petabyt/camlib)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <camlib.h>
#include <ptp.h>

void ptp_reset(struct PtpRuntime *r) {
	r->io_kill_switch = 1;
	r->transaction = 0;
	r->session = 0;	
	r->connection_type = PTP_USB;
	r->caller_unlocks_mutex = 0;
	r->wait_for_response = 1;
}

void ptp_init(struct PtpRuntime *r) {
	memset(r, 0, sizeof(struct PtpRuntime));
	ptp_reset(r);

	r->data = malloc(CAMLIB_DEFAULT_SIZE);
	r->data_length = CAMLIB_DEFAULT_SIZE;

	r->avail = calloc(1, sizeof(struct PtpPropAvail));

	#ifndef CAMLIB_DONT_USE_MUTEX
	r->mutex = malloc(sizeof(pthread_mutex_t));

	// We want recursive mutex, so lock can be called multiple times
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

	if (pthread_mutex_init(r->mutex, &attr)) {
		ptp_verbose_log("Failed to init mutex\n");
		free(r->mutex);
		r->mutex = NULL;
	}
	#endif
}

struct PtpRuntime *ptp_new(int options) {
	struct PtpRuntime *r = malloc(sizeof(struct PtpRuntime));
	ptp_init(r);

	// TODO: Working but can maybe add more options?
	if (options & PTP_IP) {
		r->connection_type = PTP_IP;
	} else if (options & PTP_USB) {
		r->connection_type = PTP_USB;
	} else if (options & PTP_IP_USB) {
		r->connection_type = PTP_IP_USB;
	}

	return r;
}

void ptp_set_prop_avail_info(struct PtpRuntime *r, int code, int memb_size, int cnt, void *data) {
	// Traverse backwards to first item
	struct PtpPropAvail *n;
	for (n = r->avail; n != NULL; n = n->prev) {
		if (n->code == code) break;
	}

	if (n != NULL) {
		// Only realloc if needed (eventually will stop allocating once we have hit a maximum)
		if (cnt > n->memb_cnt) {
			n->data = realloc(n->data, memb_size * cnt);
		}

		memcpy(n->data, data, memb_size * cnt);
		return;
	}

	// Handle first element of linked list
	if (r->avail->code == 0x0) {
		n = r->avail;
	} else {
		n = calloc(1, sizeof(struct PtpPropAvail));
		r->avail->next = n;
		n->prev = r->avail;
	}

	n->code = code;
	n->memb_size = memb_size;
	n->memb_cnt = cnt;
	void *dup = malloc(memb_size * cnt);
	memcpy(dup, data, memb_size * cnt);
	n->data = dup;

	r->avail = n;
}

void ptpusb_free_device_list(struct PtpDeviceEntry *e) {
	struct PtpDeviceEntry *next;
	while (e != NULL) {
		next = e->next;
		free(e);
		e = next;
	}
}

int ptp_buffer_resize(struct PtpRuntime *r, size_t size) {
	// realloc with a little extra space to minimize reallocs later on
	static int extra = 100;
	ptp_verbose_log("Extending IO buffer to %X\n", size + extra);
	r->data = realloc(r->data, size + extra);
	r->data_length = size + extra;
	if (r->data == NULL) {
		return PTP_OUT_OF_MEM;
	}

	return 0;
}

void ptp_mutex_lock(struct PtpRuntime *r) {
	if (r->mutex == NULL) return;
	pthread_mutex_lock(r->mutex);
}

void ptp_mutex_keep_locked(struct PtpRuntime *r) {
	if (r->mutex == NULL) return;
	pthread_mutex_lock(r->mutex);
}

// 'pop' the mutex stack, will only unlock the mutex once stack is at zero
void ptp_mutex_unlock(struct PtpRuntime *r) {
	if (r->mutex == NULL) return;
	pthread_mutex_unlock(r->mutex);
}

void ptp_close(struct PtpRuntime *r) {
	free(r->data);
}

// Perform a generic command transaction - no data phase
int ptp_send(struct PtpRuntime *r, struct PtpCommand *cmd) {
	ptp_mutex_lock(r);

	r->data_phase_length = 0;

	int length = ptp_new_cmd_packet(r, cmd);
	if (ptp_send_bulk_packets(r, length) != length) {
		ptp_mutex_unlock(r);
		ptp_verbose_log("Didn't send all packets\n");
		return PTP_IO_ERR;
	}

	if (ptp_receive_bulk_packets(r) < 0) {
		ptp_mutex_unlock(r);
		ptp_verbose_log("Failed to recieve packets\n");
		return PTP_IO_ERR;
	}

	// if (ptp_get_last_transaction_id(r) != r->transaction) {
		// ptp_verbose_log("Mismatch transaction ID\n");
		// ptp_mutex_unlock(r);
		// return PTP_IO_ERR;
	// }

	r->transaction++;

	if (ptp_get_return_code(r) == PTP_RC_OK) {
		if (!r->caller_unlocks_mutex) ptp_mutex_unlock(r);
		return 0;
	} else {
		ptp_verbose_log("Invalid return code: %X\n", ptp_get_return_code(r));
		if (!r->caller_unlocks_mutex) ptp_mutex_unlock(r);
		return PTP_CHECK_CODE;
	}
}

// Perform a command request with a data phase to the camera
int ptp_send_data(struct PtpRuntime *r, struct PtpCommand *cmd, void *data, int length) {
	ptp_mutex_lock(r);

	// Required for libWPD and PTP/IP
	r->data_phase_length = length;

	// These numbers are not exact, but it's fine
	if (length + 50 > r->data_length) {
		ptp_buffer_resize(r, 100 + length);
	}

	// Send operation request (data phase later on)
	int plength = ptp_new_cmd_packet(r, cmd);
	if (ptp_send_bulk_packets(r, plength) != plength) {
		ptp_mutex_unlock(r);
		return PTP_IO_ERR;
	}

	if (r->connection_type == PTP_IP) {
		// Send data start packet first (only has payload length)
		plength = ptpip_data_start_packet(r, length);
		if (ptp_send_bulk_packets(r, plength) != plength) {
			ptp_mutex_unlock(r);
			return PTP_IO_ERR;
		}

		// Send data end packet, with payload
		plength = ptpip_data_end_packet(r, data, length);
		if (ptp_send_bulk_packets(r, plength) != plength) {
			ptp_mutex_unlock(r);
			return PTP_IO_ERR;
		}
	} else {
		// Single data packet
		plength = ptp_new_data_packet(r, cmd, data, length);
		if (ptp_send_bulk_packets(r, plength) != plength) {
			ptp_mutex_unlock(r);
			ptp_verbose_log("Failed to send data packet (%d)\n", plength);
			return PTP_IO_ERR;
		}
	}

	if (ptp_receive_bulk_packets(r) < 0) {
		ptp_mutex_unlock(r);
		return PTP_IO_ERR;
	}

	// TODO: doesn't work on windows (IDs are made up)
	//if (ptp_get_last_transaction_id(r) != r->transaction) {
		//ptp_verbose_log("ptp_send_data: Mismatch transaction ID (%d/%d)\n", ptp_get_last_transaction_id(r), r->transaction);
		//ptp_mutex_unlock(r);
		//return PTP_IO_ERR;
	//}

	r->transaction++;

	if (ptp_get_return_code(r) == PTP_RC_OK) {
		if (!r->caller_unlocks_mutex) ptp_mutex_unlock(r);
		return 0;
	} else {
		if (!r->caller_unlocks_mutex) ptp_mutex_unlock(r);
		return PTP_CHECK_CODE;
	}
}

int ptp_device_type(struct PtpRuntime *r) {
	struct PtpDeviceInfo *di = r->di;
	if (di == NULL) return PTP_DEV_EMPTY;
	if (!strcmp(di->manufacturer, "Canon Inc.")) {
		if (ptp_check_opcode(r, PTP_OC_EOS_GetStorageIDs)) {
			return PTP_DEV_EOS;
		}

		return PTP_DEV_CANON;
	} else if (!strcmp(di->manufacturer, "FUJIFILM")) {
		return PTP_DEV_FUJI;
	} else if (!strcmp(di->manufacturer, "Sony Corporation")) {
		return PTP_DEV_SONY;
	} else if (!strcmp(di->manufacturer, "Nikon Corporation")) {
		return PTP_DEV_NIKON;
	}

	return PTP_DEV_EMPTY;
}

int ptp_check_opcode(struct PtpRuntime *r, int op) {
	if (r->di == NULL) return 0;
	for (int i = 0; i < r->di->ops_supported_length; i++) {
		if (r->di->ops_supported[i] == op) {
			return 1;
		}
	}

	return 0;
}

int ptp_check_prop(struct PtpRuntime *r, int code) {
	if (r->di == NULL) return 0;
	for (int i = 0; i < r->di->props_supported_length; i++) {
		if (r->di->props_supported[i] == code) {
			return 1;
		}
	}

	return 0;
}

int ptp_dump(struct PtpRuntime *r) {
	FILE *f = fopen("DUMP", "w");
	fwrite(r->data, r->data_length, 1, f);
	fclose(f);
	return 0;
}
