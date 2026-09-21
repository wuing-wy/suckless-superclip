# Extension protocol v1

[中文](PROTOCOL_CN.md)

This page describes the required v1 behavior. [SPEC.md](../SPEC.md#9-扩展协议-v1)
is the normative definition. The current frontend does not yet implement every
rule, including `STATUS` handling and complete request-ID validation; see
[PROGRESS.md](../PROGRESS.md).

## Encoding

The protocol is valid UTF-8. Each record ends with LF and contains tab-separated
fields. NUL is invalid. Within a field, encode backslash, tab, LF, and CR as
`\\`, `\t`, `\n`, and `\r`. Any other backslash escape is an error.

An encoded record may be at most 65,536 bytes including its final LF. A query
may contain at most 256 `ITEM` records. Implementations may use lower local
limits.

stdout carries protocol records only. Use stderr for diagnostics.

## Description and setup

superclip invokes `extension --superclip-describe`. The extension returns
exactly one record:

```text
SUPERCLIP<TAB>1<TAB>name<TAB>short|persistent<TAB>enter|change<TAB>display-name
```

`name` must match the executable's file name. The valid mode/trigger pairs are
`short + enter`, `persistent + enter`, and `persistent + change`.

For `extension --superclip-setup`, return setup records from this set:

```text
NONE
AUTOSTART<TAB>label<TAB>command<TAB>description
STATUS<TAB>ok|warning|error<TAB>message
```

The `command` field is text shown to the user.

## Short process

For each query, superclip starts `extension --superclip-query`, writes one
record, and closes stdin:

```text
QUERY<TAB>request-id<TAB>query
```

The response is:

```text
BEGIN<TAB>request-id
ITEM<TAB>request-id<TAB>result-id<TAB>title<TAB>description
END<TAB>request-id
```

Return `BEGIN` and `END` even when there are no items. Before `BEGIN`, the
extension may instead return `ERROR<TAB>request-id<TAB>message`.

For an action, superclip starts `extension --superclip-execute` and writes:

```text
EXECUTE<TAB>request-id<TAB>query<TAB>result-id
```

Return one of:

```text
OK<TAB>request-id
ERROR<TAB>request-id<TAB>message
```

The original query is included because the query process has already exited.

## Persistent process

superclip starts `extension --superclip-session` once for the current panel.
Its stdin accepts any number of `QUERY` and `EXECUTE` records, followed by:

```text
QUIT
```

Responses use the same records as short mode and may be interleaved. Every
response record must use the request ID of its request. Once a newer query is
sent, responses for older queries are read but not displayed. Exit after
`QUIT` or EOF.

## Failure rules

Malformed UTF-8, bad escaping, unknown records, wrong field counts, unexpected
request IDs, oversized records, too many items, and a missing terminator fail
the current extension session. A valid response for a stale query is the
exception: it is drained and ignored. After the item limit is reached,
superclip still reads through the matching `END` so the stream stays
synchronized.

Result IDs are opaque strings returned unchanged to the extension. A successful
action closes the panel; an action error remains visible in the open panel.
