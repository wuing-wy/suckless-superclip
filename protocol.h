#ifndef SUPERCLIP_PROTOCOL_H
#define SUPERCLIP_PROTOCOL_H

#include <stddef.h>

enum sc_record_kind {
	SC_RECORD_BEGIN,
	SC_RECORD_ITEM,
	SC_RECORD_END,
	SC_RECORD_ERROR,
	SC_RECORD_OK,
	SC_RECORD_QUERY,
	SC_RECORD_EXECUTE,
	SC_RECORD_QUIT,
	SC_RECORD_SUPERCLIP,
	SC_RECORD_NONE,
	SC_RECORD_AUTOSTART,
	SC_RECORD_STATUS
};

struct sc_record {
	enum sc_record_kind kind;
	char **fields;
	size_t nfields;
};

int sc_protocol_escape(const char *in, size_t len, char **out, size_t *outlen);
int sc_protocol_serialize(const char *const *fields, size_t nfields, char **out, size_t *outlen);
int sc_protocol_parse(const char *line, size_t len, struct sc_record *out);
int sc_protocol_validate(const struct sc_record *record);
void sc_record_free(struct sc_record *record);
const char *sc_record_name(enum sc_record_kind kind);

#endif
