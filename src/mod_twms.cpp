/*
 *
 * An OnEarth protocol module, converts a tWMS request to a M/L/R/C tile request
 * Lucian Plesea
 * (C) 2016
 *
 */

#include "mod_twms.h"

static void *create_dir_config(apr_pool_t *p, char *dummy)
{
}

static void register_hooks(apr_pool_t *p) {

}

static const command_rec cmds[] = {
    {NULL}
};

module AP_MODULE_DECLARE_DATA twms_module = {
    STANDARD20_MODULE_STUFF,
    create_dir_config,
    0,
    0,
    0,
    cmds,
    register_hooks
};
