// Minimal stub of glibc <argp.h> for the wasm build.
//
// framework-portduino's Arduino.h includes <argp.h> unconditionally, and
// PortduinoGlue.cpp DEFINES argp option tables / a parse_opt callback - but
// nothing in the wasm build CALLS argp_parse (that lived in the framework
// main.cpp we replaced with portduino_main_wasm.cpp). So only the types and
// macros are needed to compile; no argp functions are linked.
#ifndef WASM_ARGP_STUB_H
#define WASM_ARGP_STUB_H

#include <errno.h>

#ifndef ARGP_ERR_UNKNOWN
#define ARGP_ERR_UNKNOWN E2BIG
#endif
#define ARGP_KEY_ARG 0
#define ARGP_KEY_INIT 0x1000002
#define ARGP_KEY_END 0x1000001
#define OPTION_ARG_OPTIONAL 0x1

#ifdef __cplusplus
extern "C" {
#endif

typedef int error_t;

struct argp_state {
    const struct argp *argp;
    int argc;
    char **argv;
    int next;
    unsigned flags;
    unsigned arg_num;
    int quoted;
    void *input;
    void **child_inputs;
    void *hook;
    char *name;
    void *err_stream;
    void *out_stream;
    void *pstate;
};

struct argp_option {
    const char *name;
    int key;
    const char *arg;
    int flags;
    const char *doc;
    int group;
};

typedef error_t (*argp_parser_t)(int key, char *arg, struct argp_state *state);

struct argp_child {
    const struct argp *argp;
    int flags;
    const char *header;
    int group;
};

struct argp {
    const struct argp_option *options;
    argp_parser_t parser;
    const char *args_doc;
    const char *doc;
    const struct argp_child *children;
    char *(*help_filter)(int key, const char *text, void *input);
    const char *argp_domain;
};

extern const char *argp_program_version;
extern const char *argp_program_bug_address;

#ifdef __cplusplus
}
#endif
#endif // WASM_ARGP_STUB_H
