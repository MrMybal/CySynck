#include <cysynck/c_api.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static int run(int argc, char **argv) {
    char *output = NULL;
    cysynck_handle core;
    int result;
    if (argc < 2 || argc > 5) {
        fprintf(stderr, "Usage: cysynck-c-consumer STATE [METHOD [API_PATH [JSON_BODY]]]\n");
        return 2;
    }
    if (cysynck_abi_version() != 1) {
        fprintf(stderr, "Unsupported CySynck C ABI\n");
        return 2;
    }
    core = cysynck_open(argv[1], &output);
    if (!core) {
        fprintf(stderr, "%s\n", output ? output : "Cannot open CySynck");
        cysynck_free(output);
        return 1;
    }
    cysynck_free(output);
    output = NULL;
    result = cysynck_request(core, argc > 2 ? argv[2] : "GET", argc > 3 ? argv[3] : "/api/overview",
                             argc > 4 ? argv[4] : "{}", &output);
    if (output)
        fprintf(result ? stderr : stdout, "%s\n", output);
    else
        fprintf(stderr, "CySynck returned no JSON output\n");
    cysynck_free(output);
    cysynck_close(core);
    return result ? 1 : 0;
}

#ifdef _WIN32
/* The C ABI expects UTF-8 even on Windows. Convert wide process arguments explicitly. */
int wmain(int argc, wchar_t **wide_argv) {
    char **argv = (char **)calloc((size_t)argc + 1, sizeof(char *));
    int result = 2, i;
    if (!argv)
        return result;
    for (i = 0; i < argc; ++i) {
        int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1, NULL, 0,
                                        NULL, NULL);
        if (!bytes)
            goto cleanup;
        argv[i] = (char *)malloc((size_t)bytes);
        if (!argv[i] || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1,
                                             argv[i], bytes, NULL, NULL))
            goto cleanup;
    }
    result = run(argc, argv);
cleanup:
    for (i = 0; i < argc; ++i)
        free(argv[i]);
    free(argv);
    return result;
}
#else
int main(int argc, char **argv) { return run(argc, argv); }
#endif
