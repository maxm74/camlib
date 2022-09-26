#ifndef OPERATIONS_H
#define OPERATIONS_H

int ptp_open_session(struct PtpRuntime *r);
int ptp_close_session(struct PtpRuntime *r);
int ptp_get_device_info(struct PtpRuntime *r, struct PtpDeviceInfo *di);
int ptp_get_storage_ids(struct PtpRuntime *r, struct PtpStorageIds *si);
int ptp_canon_evproc(struct PtpRuntime *r, char *string);

#endif
