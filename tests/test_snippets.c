#include "protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;

#define CHECK(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

static const char *
repo_root(void)
{
	static char root[1024];
	char cwd[768];
	int n;

	if (getcwd(cwd, sizeof(cwd)) == NULL)
		return NULL;
	n = snprintf(root, sizeof(root), "%s", cwd);
	return n > 0 && (size_t)n < sizeof(root) ? root : NULL;
}

static int
write_file(const char *path, const char *data)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	ssize_t n;
	size_t len, off = 0;

	if (fd < 0)
		return -1;
	len = strlen(data);
	while (off < len) {
		n = write(fd, data + off, len - off);
		if (n > 0) {
			off += (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		close(fd);
		return -1;
	}
	return close(fd);
}

/* Run snippets-extension with argv + optional stdin, capture stdout/stderr. */
static int
run_extension(char *const *argv, const char *config_dir, const char *runtime_dir,
    const char *input, char *out, size_t outcap, char *err, size_t errcap, int *status)
{
	int in[2] = { -1, -1 }, o[2] = { -1, -1 }, e[2] = { -1, -1 };
	pid_t pid;
	const char *root;

	root = repo_root();
	if (root == NULL || pipe(in) < 0 || pipe(o) < 0 || pipe(e) < 0)
		return -1;
	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		char bin[2048];
		int n;

		if (dup2(in[0], STDIN_FILENO) < 0 || dup2(o[1], STDOUT_FILENO) < 0 ||
		    dup2(e[1], STDERR_FILENO) < 0)
			_exit(127);
		close(in[0]); close(in[1]); close(o[0]); close(o[1]); close(e[0]); close(e[1]);
		n = snprintf(bin, sizeof(bin), "%s/snippets-extension", root);
		if (n < 0 || (size_t)n >= sizeof(bin))
			_exit(127);
		if (config_dir != NULL)
			(void)setenv("XDG_CONFIG_HOME", config_dir, 1);
		else
			(void)unsetenv("XDG_CONFIG_HOME");
		(void)setenv("HOME", "/nonexistent-snippets-test", 1);
		if (runtime_dir != NULL)
			(void)setenv("XDG_RUNTIME_DIR", runtime_dir, 1);
		else
			(void)unsetenv("XDG_RUNTIME_DIR");
		execv(bin, argv);
		_exit(127);
	}
	close(in[0]); close(o[1]); close(e[1]);
	if (input != NULL) {
		size_t left = strlen(input), off = 0;

		while (off < left) {
			ssize_t n = write(in[1], input + off, left - off);

			if (n > 0) {
				off += (size_t)n;
				continue;
			}
			if (n < 0 && errno == EINTR)
				continue;
			break;
		}
	}
	close(in[1]);
	for (;;) {
		struct pollfd pfds[2];
		ssize_t n;
		char buf[1024];
		size_t olen = strlen(out), elen = strlen(err);

		pfds[0].fd = o[0];
		pfds[0].events = POLLIN | POLLHUP;
		pfds[0].revents = 0;
		pfds[1].fd = e[0];
		pfds[1].events = POLLIN | POLLHUP;
		pfds[1].revents = 0;
		if (poll(pfds, 2, 3000) <= 0)
			break;
		if ((pfds[0].revents & (POLLIN | POLLHUP)) != 0) {
			n = read(o[0], buf, sizeof(buf) - 1);
			if (n > 0 && olen + (size_t)n + 1 < outcap) {
				memcpy(out + olen, buf, (size_t)n);
				out[olen + (size_t)n] = '\0';
			} else if (n == 0) {
				pfds[0].fd = -1;
			}
		}
		if ((pfds[1].revents & (POLLIN | POLLHUP)) != 0) {
			n = read(e[0], buf, sizeof(buf) - 1);
			if (n > 0 && elen + (size_t)n + 1 < errcap) {
				memcpy(err + elen, buf, (size_t)n);
				err[elen + (size_t)n] = '\0';
			} else if (n == 0) {
				pfds[1].fd = -1;
			}
		}
		if (pfds[0].fd < 0 && pfds[1].fd < 0)
			break;
	}
	close(o[0]);
	close(e[0]);
	for (;;) {
		int ret = waitpid(pid, status, 0);

		if (ret == pid)
			return 0;
		if (ret < 0 && errno != EINTR)
			return -1;
	}
}

static void
make_dirs(const char *config_dir, const char *file)
{
	char dir[2048];
	int n;

	n = snprintf(dir, sizeof(dir), "%s/superclip", config_dir);
	if (n < 0 || (size_t)n >= sizeof(dir)) {
		CHECK(0 && "config dir too long");
		return;
	}
	(void)mkdir(dir, 0700);
	(void)mkdir(config_dir, 0700);
	(void)write_file(file, "");
}

static void
test_describe_setup(void)
{
	char *describe[] = { (char *)"snippets-extension", (char *)"--superclip-describe", NULL };
	char *setup[] = { (char *)"snippets-extension", (char *)"--superclip-setup", NULL };
	char out[1024] = "", err[1024] = "";
	int status = 0;

	CHECK(run_extension(describe, NULL, NULL, NULL, out, sizeof(out), err, sizeof(err),
	    &status) == 0);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	CHECK(strcmp(out, "SUPERCLIP\t1\tsnippets\tpersistent\tchange\tSnippets\n") == 0);
	out[0] = '\0';
	err[0] = '\0';
	CHECK(run_extension(setup, NULL, NULL, NULL, out, sizeof(out), err, sizeof(err),
	    &status) == 0);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	CHECK(strcmp(out, "NONE\n") == 0);
}

static void
test_missing_is_empty(void)
{
	char template[] = "/tmp/snippets-missing.XXXXXX";
	char runtime[] = "/tmp/snippets-rt-missing.XXXXXX";
	char *session[] = { (char *)"snippets-extension", (char *)"--superclip-session", NULL };
	char out[4096] = "", err[4096] = "";
	int status = 0;

	CHECK(mkdtemp(template) != NULL);
	CHECK(mkdtemp(runtime) != NULL);
	CHECK(run_extension(session, template, runtime, "QUERY\t1\t\nQUIT\n", out, sizeof(out),
	    err, sizeof(err), &status) == 0);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	CHECK(strcmp(out, "BEGIN\t1\nEND\t1\n") == 0);
}

static void
test_parse_filter(void)
{
	char template[] = "/tmp/snippets-parse.XXXXXX";
	char runtime[] = "/tmp/snippets-rt-parse.XXXXXX";
	char file[1024], out[8192] = "", err[4096] = "";
	char *session[] = { (char *)"snippets-extension", (char *)"--superclip-session", NULL };
	int status = 0;

	CHECK(mkdtemp(template) != NULL);
	CHECK(mkdtemp(runtime) != NULL);
	(void)snprintf(file, sizeof(file), "%s/superclip/snippets", template);
	make_dirs(template, file);
	CHECK(write_file(file,
	    "# comment\n"
	    "\n"
	    "deploy\tkubectl restart\\nsecond\n"
	    "bad-no-tab\n"
	    "bad-escape\\tfoo\\xbar\n"
	    "daily\tToday I did \\t stuff\n") == 0);
	CHECK(run_extension(session, template, runtime,
	    "QUERY\t7\t\n"
	    "QUERY\t8\tdeploy\n"
	    "QUERY\t9\tDEPLOY\n"
	    "QUIT\n",
	    out, sizeof(out), err, sizeof(err), &status) == 0);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	CHECK(strstr(out, "BEGIN\t7\n") != NULL);
	CHECK(strstr(out, "ITEM\t7\t3:deploy\tdeploy\tkubectl restart\n") != NULL);
	CHECK(strstr(out, "ITEM\t7\t6:daily\tdaily\tToday I did \\t stuff\n") != NULL);
	CHECK(strstr(out, "END\t7\n") != NULL);
	CHECK(strstr(out, "BEGIN\t8\nITEM\t8\t3:deploy\tdeploy\tkubectl restart\nEND\t8\n") != NULL);
	CHECK(strstr(out, "BEGIN\t9\nEND\t9\n") != NULL);
	CHECK(strstr(err, "snippets: skipped 2 bad lines") != NULL);
}

static void
test_execute_unknown(void)
{
	char template[] = "/tmp/snippets-exec.XXXXXX";
	char runtime[] = "/tmp/snippets-rt-exec.XXXXXX";
	char file[1024], out[4096] = "", err[4096] = "";
	char *session[] = { (char *)"snippets-extension", (char *)"--superclip-session", NULL };
	int status = 0;

	CHECK(mkdtemp(template) != NULL);
	CHECK(mkdtemp(runtime) != NULL);
	(void)snprintf(file, sizeof(file), "%s/superclip/snippets", template);
	make_dirs(template, file);
	CHECK(write_file(file, "one\tfirst body\ntwo\tsecond body\n") == 0);
	CHECK(run_extension(session, template, runtime,
	    "EXECUTE\t3\t\t99:nope\n"
	    "EXECUTE\t4\t\t1:wrong-title\n"
	    "QUIT\n",
	    out, sizeof(out), err, sizeof(err), &status) == 0);
	CHECK(strstr(out, "ERROR\t3\tunknown snippet\n") != NULL);
	CHECK(strstr(out, "ERROR\t4\tunknown snippet\n") != NULL);
}

static void
test_limits(void)
{
	char template[] = "/tmp/snippets-limits.XXXXXX";
	char runtime[] = "/tmp/snippets-rt-limits.XXXXXX";
	char file[1024], out[131072] = "", err[4096] = "";
	char *cli[] = { (char *)"snippets-extension", (char *)"--superclip-query", NULL };
	char *big = NULL;
	size_t i;
	int status = 0;

	CHECK(mkdtemp(template) != NULL);
	CHECK(mkdtemp(runtime) != NULL);
	(void)snprintf(file, sizeof(file), "%s/superclip/snippets", template);
	make_dirs(template, file);
	big = malloc(300 * 64);
	CHECK(big != NULL);
	big[0] = '\0';
	for (i = 0; i < 300; i++) {
		char line[64];

		(void)snprintf(line, sizeof(line), "t%zu\tbody%zu\n", i, i);
		strcat(big, line);
	}
	CHECK(write_file(file, big) == 0);
	free(big);
	CHECK(run_extension(cli, template, runtime, "QUERY\t21\t\n", out, sizeof(out),
	    err, sizeof(err), &status) == 0);
	for (i = 0; i < 256; i++) {
		char want[64];

		(void)snprintf(want, sizeof(want), "ITEM\t21\t%zu:t%zu\t", i + 1, i);
		CHECK(strstr(out, want) != NULL);
	}
	CHECK(strstr(out, "ITEM\t21\t257:") == NULL);
	CHECK(strstr(out, "END\t21\n") != NULL);
}

static void
test_stale_title_recheck(void)
{
	char template[] = "/tmp/snippets-toctou.XXXXXX";
	char runtime[] = "/tmp/snippets-rt-toctou.XXXXXX";
	char file[1024], out[8192] = "", err[4096] = "";
	char *cli[] = { (char *)"snippets-extension", (char *)"--superclip-query", NULL };
	char *cli2[] = { (char *)"snippets-extension", (char *)"--superclip-query", NULL };
	int status = 0;

	CHECK(mkdtemp(template) != NULL);
	CHECK(mkdtemp(runtime) != NULL);
	(void)snprintf(file, sizeof(file), "%s/superclip/snippets", template);
	make_dirs(template, file);
	CHECK(write_file(file, "alpha\tfirst\nbeta\tsecond\n") == 0);
	CHECK(run_extension(cli, template, runtime, "QUERY\t11\t\n", out, sizeof(out),
	    err, sizeof(err), &status) == 0);
	CHECK(strstr(out, "ITEM\t11\t1:alpha\t") != NULL);
	CHECK(write_file(file, "alpha\tCHANGED\nbeta\tsecond\n") == 0);
	out[0] = '\0';
	err[0] = '\0';
	CHECK(run_extension(cli2, template, runtime, "EXECUTE\t12\t\t1:alpha\n", out,
	    sizeof(out), err, sizeof(err), &status) == 0);
	CHECK(strstr(out, "ERROR\t12\tunknown snippet") == NULL);
	CHECK(strstr(out, "OK\t12") != NULL || strstr(out, "ERROR\t12") != NULL);
	CHECK(write_file(file, "other\tfirst\nbeta\tsecond\n") == 0);
	out[0] = '\0';
	err[0] = '\0';
	CHECK(run_extension(cli2, template, runtime, "EXECUTE\t13\t\t1:alpha\n", out,
	    sizeof(out), err, sizeof(err), &status) == 0);
	CHECK(strstr(out, "ERROR\t13\tunknown snippet\n") != NULL);
}

int
main(void)
{
	test_describe_setup();
	test_missing_is_empty();
	test_parse_filter();
	test_execute_unknown();
	test_limits();
	test_stale_title_recheck();
	if (failures != 0) {
		fprintf(stderr, "test_snippets: %d failures\n", failures);
		return 1;
	}
	return 0;
}
