#include "test-tool.h"
#include "strvec.h"
#include "run-command.h"
#include "exec-cmd.h"
#include "repository.h"
#include "trace2.h"

typedef int(fn_unit_test)(int argc, const char **argv);

struct unit_test {
	fn_unit_test *ut_fn;
	const char *ut_name;
	const char *ut_usage;
};

#define MyOk 0
#define MyError 1

static int get_i(int *p_value, const char *data)
{
	char *endptr;

	if (!data || !*data)
		return MyError;

	*p_value = strtol(data, &endptr, 10);
	if (*endptr || errno == ERANGE)
		return MyError;

	return MyOk;
}

/*
 * Cause process to exit with the requested value via "return".
 *
 * Rely on test-tool.c:cmd_main() to call trace2_cmd_exit()
 * with our result.
 *
 * Test harness can confirm:
 * [] the process-exit value.
 * [] the "code" field in the "exit" trace2 event.
 * [] the "code" field in the "atexit" trace2 event.
 * [] the "name" field in the "cmd_name" trace2 event.
 * [] "def_param" events for all of the "interesting" pre-defined
 * config settings.
 */
static int ut_001return(int argc UNUSED, const char **argv)
{
	int rc;

	if (get_i(&rc, argv[0]))
		die("expect <exit_code>");

	return rc;
}

/*
 * Cause the process to exit with the requested value via "exit()".
 *
 * Test harness can confirm:
 * [] the "code" field in the "exit" trace2 event.
 * [] the "code" field in the "atexit" trace2 event.
 * [] the "name" field in the "cmd_name" trace2 event.
 * [] "def_param" events for all of the "interesting" pre-defined
 * config settings.
 */
static int ut_002exit(int argc UNUSED, const char **argv)
{
	int rc;

	if (get_i(&rc, argv[0]))
		die("expect <exit_code>");

	exit(rc);
}

/*
 * Send an "error" event with each value in argv.  Normally, git only issues
 * a single "error" event immediately before issuing an "exit" event (such
 * as in die() or BUG()), but multiple "error" events are allowed.
 *
 * Test harness can confirm:
 * [] a trace2 "error" event for each value in argv.
 * [] the "name" field in the "cmd_name" trace2 event.
 * [] (optional) the file:line in the "exit" event refers to this function.
 */
static int ut_003error(int argc, const char **argv)
{
	int k;

	if (!argv[0] || !*argv[0])
		die("expect <error_message>");

	for (k = 0; k < argc; k++)
		error("%s", argv[k]);

	return 0;
}

/*
 * Run a child process and wait for it to finish and exit with its return code.
 * test-tool trace2 004child [<child-command-line>]
 *
 * For example:
 * test-tool trace2 004child git version
 * test-tool trace2 004child test-tool trace2 001return 0
 * test-tool trace2 004child test-tool trace2 004child test-tool trace2 004child
 * test-tool trace2 004child git -c alias.xyz=version xyz
 *
 * Test harness can confirm:
 * [] the "name" field in the "cmd_name" trace2 event.
 * [] that the outer process has a single component SID (or depth "d0" in
 *    the PERF stream).
 * [] that "child_start" and "child_exit" events are generated for the child.
 * [] if the child process is an instrumented executable:
 *    [] that "version", "start", ..., "exit", and "atexit" events are
 *       generated by the child process.
 *    [] that the child process events have a multiple component SID (or
 *       depth "dN+1" in the PERF stream).
 * [] that the child exit code is propagated to the parent process "exit"
 *    and "atexit" events..
 * [] (optional) that the "t_abs" field in the child process "atexit" event
 *    is less than the "t_rel" field in the "child_exit" event of the parent
 *    process.
 * [] if the child process is like the alias example above,
 *    [] (optional) the child process attempts to run "git-xyx" as a dashed
 *       command.
 *    [] the child process emits an "alias" event with "xyz" => "version"
 *    [] the child process runs "git version" as a child process.
 *    [] the child process has a 3 component SID (or depth "d2" in the PERF
 *       stream).
 */
static int ut_004child(int argc, const char **argv)
{
	struct child_process cmd = CHILD_PROCESS_INIT;
	int result;

	/*
	 * Allow empty <child_command_line> so we can do arbitrarily deep
	 * command nesting and let the last one be null.
	 */
	if (!argc)
		return 0;

	strvec_pushv(&cmd.args, argv);
	result = run_command(&cmd);
	exit(result);
}

/*
 * Exec a git command.  This may either create a child process (Windows)
 * or replace the existing process.
 * test-tool trace2 005exec <git_command_args>
 *
 * For example:
 * test-tool trace2 005exec version
 *
 * Test harness can confirm (on Windows):
 * [] the "name" field in the "cmd_name" trace2 event.
 * [] that the outer process has a single component SID (or depth "d0" in
 *    the PERF stream).
 * [] that "exec" and "exec_result" events are generated for the child
 *    process (since the Windows compatibility layer fakes an exec() with
 *    a CreateProcess(), WaitForSingleObject(), and exit()).
 * [] that the child process has multiple component SID (or depth "dN+1"
 *    in the PERF stream).
 *
 * Test harness can confirm (on platforms with a real exec() function):
 * [] TODO talk about process replacement and how it affects SID.
 */
static int ut_005exec(int argc, const char **argv)
{
	int result;

	if (!argc)
		return 0;

	result = execv_git_cmd(argv);
	return result;
}

static int ut_006data(int argc, const char **argv)
{
	const char *usage_error =
		"expect <cat0> <k0> <v0> [<cat1> <k1> <v1> [...]]";

	if (argc % 3 != 0)
		die("%s", usage_error);

	while (argc) {
		if (!argv[0] || !*argv[0] || !argv[1] || !*argv[1] ||
		    !argv[2] || !*argv[2])
			die("%s", usage_error);

		trace2_data_string(argv[0], the_repository, argv[1], argv[2]);
		argv += 3;
		argc -= 3;
	}

	return 0;
}

static int ut_007BUG(int argc UNUSED, const char **argv UNUSED)
{
	/*
	 * Exercise BUG() to ensure that the message is printed to trace2.
	 */
	BUG("the bug message");
}

static int ut_008bug(int argc UNUSED, const char **argv UNUSED)
{
	bug("a bug message");
	bug("another bug message");
	BUG_if_bug("an explicit BUG_if_bug() following bug() call(s) is nice, but not required");
	return 0;
}

static int ut_009bug_BUG(int argc UNUSED, const char **argv UNUSED)
{
	bug("a bug message");
	bug("another bug message");
	/* The BUG_if_bug(...) isn't here, but we'll spot bug() calls on exit()! */
	return 0;
}

static int ut_010bug_BUG(int argc UNUSED, const char **argv UNUSED)
{
	bug("a %s message", "bug");
	BUG("a %s message", "BUG");
}

/*
 * Single-threaded timer test.  Create several intervals using the
 * TEST1 timer.  The test script can verify that an aggregate Trace2
 * "timer" event is emitted indicating that we started+stopped the
 * timer the requested number of times.
 */
static int ut_100timer(int argc, const char **argv)
{
	const char *usage_error =
		"expect <count> <ms_delay>";

	int count = 0;
	int delay = 0;
	int k;

	if (argc != 2)
		die("%s", usage_error);
	if (get_i(&count, argv[0]))
		die("%s", usage_error);
	if (get_i(&delay, argv[1]))
		die("%s", usage_error);

	for (k = 0; k < count; k++) {
		trace2_timer_start(TRACE2_TIMER_ID_TEST1);
		sleep_millisec(delay);
		trace2_timer_stop(TRACE2_TIMER_ID_TEST1);
	}

	return 0;
}

struct ut_101_data {
	int count;
	int delay;
};

static void *ut_101timer_thread_proc(void *_ut_101_data)
{
	struct ut_101_data *data = _ut_101_data;
	int k;

	trace2_thread_start("ut_101");

	for (k = 0; k < data->count; k++) {
		trace2_timer_start(TRACE2_TIMER_ID_TEST2);
		sleep_millisec(data->delay);
		trace2_timer_stop(TRACE2_TIMER_ID_TEST2);
	}

	trace2_thread_exit();
	return NULL;
}

/*
 * Multi-threaded timer test.  Create several threads that each create
 * several intervals using the TEST2 timer.  The test script can verify
 * that an individual Trace2 "th_timer" events for each thread and an
 * aggregate "timer" event are generated.
 */
static int ut_101timer(int argc, const char **argv)
{
	const char *usage_error =
		"expect <count> <ms_delay> <threads>";

	struct ut_101_data data = { 0, 0 };
	int nr_threads = 0;
	int k;
	pthread_t *pids = NULL;

	if (argc != 3)
		die("%s", usage_error);
	if (get_i(&data.count, argv[0]))
		die("%s", usage_error);
	if (get_i(&data.delay, argv[1]))
		die("%s", usage_error);
	if (get_i(&nr_threads, argv[2]))
		die("%s", usage_error);

	CALLOC_ARRAY(pids, nr_threads);

	for (k = 0; k < nr_threads; k++) {
		if (pthread_create(&pids[k], NULL, ut_101timer_thread_proc, &data))
			die("failed to create thread[%d]", k);
	}

	for (k = 0; k < nr_threads; k++) {
		if (pthread_join(pids[k], NULL))
			die("failed to join thread[%d]", k);
	}

	free(pids);

	return 0;
}

/*
 * Single-threaded counter test.  Add several values to the TEST1 counter.
 * The test script can verify that the final sum is reported in the "counter"
 * event.
 */
static int ut_200counter(int argc, const char **argv)
{
	const char *usage_error =
		"expect <v1> [<v2> [...]]";
	int value;
	int k;

	if (argc < 1)
		die("%s", usage_error);

	for (k = 0; k < argc; k++) {
		if (get_i(&value, argv[k]))
			die("invalid value[%s] -- %s",
			    argv[k], usage_error);
		trace2_counter_add(TRACE2_COUNTER_ID_TEST1, value);
	}

	return 0;
}

/*
 * Multi-threaded counter test.  Create seveal threads that each increment
 * the TEST2 global counter.  The test script can verify that an individual
 * "th_counter" event is generated with a partial sum for each thread and
 * that a final aggregate "counter" event is generated.
 */

struct ut_201_data {
	int v1;
	int v2;
};

static void *ut_201counter_thread_proc(void *_ut_201_data)
{
	struct ut_201_data *data = _ut_201_data;

	trace2_thread_start("ut_201");

	trace2_counter_add(TRACE2_COUNTER_ID_TEST2, data->v1);
	trace2_counter_add(TRACE2_COUNTER_ID_TEST2, data->v2);

	trace2_thread_exit();
	return NULL;
}

static int ut_201counter(int argc, const char **argv)
{
	const char *usage_error =
		"expect <v1> <v2> <threads>";

	struct ut_201_data data = { 0, 0 };
	int nr_threads = 0;
	int k;
	pthread_t *pids = NULL;

	if (argc != 3)
		die("%s", usage_error);
	if (get_i(&data.v1, argv[0]))
		die("%s", usage_error);
	if (get_i(&data.v2, argv[1]))
		die("%s", usage_error);
	if (get_i(&nr_threads, argv[2]))
		die("%s", usage_error);

	CALLOC_ARRAY(pids, nr_threads);

	for (k = 0; k < nr_threads; k++) {
		if (pthread_create(&pids[k], NULL, ut_201counter_thread_proc, &data))
			die("failed to create thread[%d]", k);
	}

	for (k = 0; k < nr_threads; k++) {
		if (pthread_join(pids[k], NULL))
			die("failed to join thread[%d]", k);
	}

	free(pids);

	return 0;
}

/*
 * Usage:
 *     test-tool trace2 <ut_name_1> <ut_usage_1>
 *     test-tool trace2 <ut_name_2> <ut_usage_2>
 *     ...
 */
#define USAGE_PREFIX "test-tool trace2"

/* clang-format off */
static struct unit_test ut_table[] = {
	{ ut_001return,   "001return", "<exit_code>" },
	{ ut_002exit,     "002exit",   "<exit_code>" },
	{ ut_003error,    "003error",  "<error_message>+" },
	{ ut_004child,    "004child",  "[<child_command_line>]" },
	{ ut_005exec,     "005exec",   "<git_command_args>" },
	{ ut_006data,     "006data",   "[<category> <key> <value>]+" },
	{ ut_007BUG,      "007bug",    "" },
	{ ut_008bug,      "008bug",    "" },
	{ ut_009bug_BUG,  "009bug_BUG","" },
	{ ut_010bug_BUG,  "010bug_BUG","" },

	{ ut_100timer,    "100timer",  "<count> <ms_delay>" },
	{ ut_101timer,    "101timer",  "<count> <ms_delay> <threads>" },

	{ ut_200counter,  "200counter", "<v1> [<v2> [<v3> [...]]]" },
	{ ut_201counter,  "201counter", "<v1> <v2> <threads>" },
};
/* clang-format on */

/* clang-format off */
#define for_each_ut(k, ut_k)			\
	for (k = 0, ut_k = &ut_table[k];	\
	     k < ARRAY_SIZE(ut_table);		\
	     k++, ut_k = &ut_table[k])
/* clang-format on */

static int print_usage(void)
{
	int k;
	struct unit_test *ut_k;

	fprintf(stderr, "usage:\n");
	for_each_ut (k, ut_k)
		fprintf(stderr, "\t%s %s %s\n", USAGE_PREFIX, ut_k->ut_name,
			ut_k->ut_usage);

	return 129;
}

/*
 * Issue various trace2 events for testing.
 *
 * We assume that these trace2 routines has already been called:
 *    [] trace2_initialize()      [common-main.c:main()]
 *    [] trace2_cmd_start()       [common-main.c:main()]
 *    [] trace2_cmd_name()        [test-tool.c:cmd_main()]
 *    [] tracd2_cmd_list_config() [test-tool.c:cmd_main()]
 * So that:
 *    [] the various trace2 streams are open.
 *    [] the process SID has been created.
 *    [] the "version" event has been generated.
 *    [] the "start" event has been generated.
 *    [] the "cmd_name" event has been generated.
 *    [] this writes various "def_param" events for interesting config values.
 *
 * We return from here and let test-tool.c::cmd_main() pass the exit
 * code to common-main.c::main(), which will use it to call
 * trace2_cmd_exit().
 */
int cmd__trace2(int argc, const char **argv)
{
	int k;
	struct unit_test *ut_k;

	argc--; /* skip over "trace2" arg */
	argv++;

	if (argc)
		for_each_ut (k, ut_k)
			if (!strcmp(argv[0], ut_k->ut_name))
				return ut_k->ut_fn(argc - 1, argv + 1);

	return print_usage();
}
