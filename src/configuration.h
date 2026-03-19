#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <stdbool.h>

typedef struct NetplayConfiguration {
    int p2p_local_player;
    const char* p2p_remote_ip;
    const char* matchmaking_ip;
    int matchmaking_port;
} NetplayConfiguration;

#if DEBUG
typedef struct TestRunnerConfiguration {
    bool enabled;
    const char* states_path;
} TestRunnerConfiguration;
#endif

typedef struct RendererConfiguration {
    const char* plugin_name;
} RendererConfiguration;

typedef struct Configuration {
    NetplayConfiguration netplay;
    RendererConfiguration renderer;
    int argc;
    const char** argv;
#if DEBUG
    TestRunnerConfiguration test;
#endif
} Configuration;

#endif
