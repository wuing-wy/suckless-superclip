#include "config.h"
#include "protocol.h"
#include "util.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct name_kind {
	const char *name;
	enum sc_record_kind kind;
};

static const struct name_kind names[] = {
	{ "BEGIN", SC_RECORD_BEGIN },
	{ "ITEM", SC_RECORD_ITEM },
	{ "END", SC_RECORD_END },
	{ "ERROR", SC_RECORD_ERROR },
	{ "OK", SC_RECORD_OK },
	{ "QUERY", SC_RECORD_QUERY },
	{ "EXECUTE", SC_RECORD_EXECUTE },
	{ "QUIT", SC_RECORD_QUIT },
	{ "SUPERCLIP", SC_RECORD_SUPERCLIP },
	{ "NONE", SC_RECORD_NONE },
	{ "AUTOSTART", SC_RECORD_AUTOSTART },
	{ "STATUS", SC_RECORD_STATUS }
};

static int
push_field(char ***fields, size_t *nfields, size_t *cap, const char *buf, size_t len)
{
	char **next;
	char *field;

	if (!sc_utf8_valid(buf, len) && len != 0)
		return -1;
	if (*nfields == *cap) {
		size_t newcap = *cap == 0 ? 4 : *cap * 2;
		if (newcap < *cap || newcap > SIZE_MAX / sizeof(**fields))
			return -1;
		next = realloc(*fields, newcap * sizeof(*next));
		if (next == NULL)
			return -1;
		*fields = next;
		*cap = newcap;
	}
	field = sc_xstrndup(buf, len);
	if (field == NULL)
		return -1;
	(*fields)[(*nfields)++] = field;
	return 0;
}

const char *
sc_record_name(enum sc_record_kind kind)
{
	size_t i;

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		if (names[i].kind == kind)
			return names[i].name;
	return NULL;
}

static int
record_kind(const char *name, enum sc_record_kind *kind)
{
	size_t i;

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		if (strcmp(name, names[i].name) == 0) {
			*kind = names[i].kind;
			return 0;
		}
	}
	return -1;
}

int
sc_protocol_escape(const char *in, size_t len, char **out, size_t *outlen)
{
	char *buf;
	size_t i, n = 0;

	if (out == NULL || outlen == NULL || (in == NULL && len != 0) || len > SUPERCLIP_MAX_RECORD)
		return -1;
	if (!sc_utf8_valid(in, len) && len != 0)
		return -1;
	if (len > (SUPERCLIP_MAX_RECORD - 1) / 2)
		return -1;
	buf = malloc(len * 2 + 1);
	if (buf == NULL)
		return -1;
	for (i = 0; i < len; i++) {
		switch (in[i]) {
		case '\\': buf[n++] = '\\'; buf[n++] = '\\'; break;
		case '\t': buf[n++] = '\\'; buf[n++] = 't'; break;
		case '\n': buf[n++] = '\\'; buf[n++] = 'n'; break;
		case '\r': buf[n++] = '\\'; buf[n++] = 'r'; break;
		default: buf[n++] = in[i]; break;
		}
	}
	buf[n] = '\0';
	*out = buf;
	*outlen = n;
	return 0;
}

int
sc_protocol_serialize(const char *const *fields, size_t nfields, char **out, size_t *outlen)
{
	char *record, *escaped = NULL;
	size_t i, used = 0, escaped_len;

	if (fields == NULL || nfields == 0 || out == NULL || outlen == NULL)
		return -1;
	record = malloc(SUPERCLIP_MAX_RECORD);
	if (record == NULL)
		return -1;
	for (i = 0; i < nfields; i++) {
		if (fields[i] == NULL ||
		    sc_protocol_escape(fields[i], strlen(fields[i]), &escaped, &escaped_len) < 0 ||
		    escaped_len > SUPERCLIP_MAX_RECORD - used - 1) {
			free(escaped);
			free(record);
			return -1;
		}
		memcpy(record + used, escaped, escaped_len);
		used += escaped_len;
		free(escaped);
		escaped = NULL;
		record[used++] = i + 1 == nfields ? '\n' : '\t';
	}
	*out = record;
	*outlen = used;
	return 0;
}

int
sc_protocol_parse(const char *line, size_t len, struct sc_record *out)
{
	char **fields = NULL;
	char *buf = NULL;
	size_t nfields = 0, cap = 0, i, start = 0, used = 0;
	enum sc_record_kind kind;
	int ret = -1;

	if (line == NULL || out == NULL || len == 0 || len > SUPERCLIP_MAX_RECORD || line[len - 1] != '\n')
		return -1;
	memset(out, 0, sizeof(*out));
	buf = malloc(len);
	if (buf == NULL)
		return -1;
	for (i = 0; i + 1 < len; i++) {
		if (line[i] == '\0')
			goto done;
		if (line[i] == '\t') {
			if (push_field(&fields, &nfields, &cap, buf + start, used - start) < 0)
				goto done;
			start = ++used;
			continue;
		}
		if (line[i] == '\\') {
			if (++i + 1 >= len)
				goto done;
			switch (line[i]) {
			case '\\': buf[used++] = '\\'; break;
			case 't': buf[used++] = '\t'; break;
			case 'n': buf[used++] = '\n'; break;
			case 'r': buf[used++] = '\r'; break;
			default: goto done;
			}
			continue;
		}
		buf[used++] = line[i];
	}
	if (push_field(&fields, &nfields, &cap, buf + start, used - start) < 0 || nfields == 0)
		goto done;
	if (record_kind(fields[0], &kind) < 0)
		goto done;
	out->kind = kind;
	out->fields = fields;
	out->nfields = nfields;
	fields = NULL;
	if (sc_protocol_validate(out) < 0) {
		sc_record_free(out);
		goto done;
	}
	ret = 0;
done:
	if (fields != NULL) {
		for (i = 0; i < nfields; i++)
			free(fields[i]);
		free(fields);
	}
	free(buf);
	return ret;
}

int
sc_protocol_validate(const struct sc_record *record)
{
	size_t want;

	if (record == NULL || record->fields == NULL || record->nfields == 0)
		return -1;
	switch (record->kind) {
	case SC_RECORD_BEGIN:
	case SC_RECORD_END:
	case SC_RECORD_OK: want = 2; break;
	case SC_RECORD_ITEM: want = 5; break;
	case SC_RECORD_ERROR: want = 3; break;
	case SC_RECORD_QUERY: want = 3; break;
	case SC_RECORD_EXECUTE: want = 4; break;
	case SC_RECORD_QUIT:
	case SC_RECORD_NONE: want = 1; break;
	case SC_RECORD_SUPERCLIP: want = 6; break;
	case SC_RECORD_AUTOSTART: want = 4; break;
	case SC_RECORD_STATUS: want = 3; break;
	default: return -1;
	}
	return record->nfields == want ? 0 : -1;
}

void
sc_record_free(struct sc_record *record)
{
	size_t i;

	if (record == NULL)
		return;
	for (i = 0; i < record->nfields; i++)
		free(record->fields[i]);
	free(record->fields);
	memset(record, 0, sizeof(*record));
}
