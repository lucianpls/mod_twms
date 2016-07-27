/*
 *
 * An OnEarth protocol module, converts a tWMS request to a M/L/R/C tile request
 * Lucian Plesea
 * (C) 2016
 *
 */

#include "mod_twms.h"
#include <receive_context.h>
#include <clocale>
#include <cmath>

static void init_rsets(apr_pool_t *p, struct TiledRaster &raster)
{
    // Clean up pagesize defaults
    raster.pagesize.c = raster.size.c;
    raster.pagesize.z = 1;

    struct rset level;
    level.width = int(1 + (raster.size.x - 1) / raster.pagesize.x);
    level.height = int(1 + (raster.size.y - 1) / raster.pagesize.y);
    level.rx = (raster.bbox.xmax - raster.bbox.xmin) / raster.size.x;
    level.ry = (raster.bbox.ymax - raster.bbox.ymin) / raster.size.y;

    // How many levels do we have
    raster.n_levels = 2 + ilogb(max(level.height, level.width) - 1);
    raster.rsets = (struct rset *)apr_pcalloc(p, sizeof(rset) * raster.n_levels);

    // Populate rsets from the bottom, the way tile protcols count levels
    // These are MRF rsets, not all of them are visible
    struct rset *r = raster.rsets + raster.n_levels - 1;
    for (int i = 0; i < raster.n_levels; i++) {
        *r-- = level;
        // Prepare for the next level, assuming powers of two
        level.width = 1 + (level.width - 1) / 2;
        level.height = 1 + (level.height - 1) / 2;
        level.rx *= 2;
        level.ry *= 2;
    }

    // MRF has one tile at the top
    ap_assert(raster.rsets[0].height == 1 && raster.rsets[0].width == 1);
    ap_assert(raster.n_levels > raster.skip);
}

// Temporary switch locale to C, get four comma separated numbers in a bounding box, WMS style
static const char *getbbox(const char *line, bbox_t *bbox)
{
    const char *lcl = setlocale(LC_NUMERIC, NULL);
    const char *message = " format incorrect, expects four comma separated C locale numbers";
    char *l;
    setlocale(LC_NUMERIC, "C");

    do {
        bbox->xmin = strtod(line, &l); if (*l++ != ',') break;
        bbox->ymin = strtod(l, &l);    if (*l++ != ',') break;
        bbox->xmax = strtod(l, &l);    if (*l++ != ',') break;
        bbox->ymax = strtod(l, &l);
        message = NULL;
    } while (false);

    setlocale(LC_NUMERIC, lcl);
    return message;
}

// Returns NULL if it worked as expected, returns a four integer value from "x y", "x y z" or "x y z c"
static const char *get_xyzc_size(struct sz *size, const char *value) {
    char *s;
    if (!value)
        return " values missing";
    size->x = apr_strtoi64(value, &s, 0);
    size->y = apr_strtoi64(s, &s, 0);
    size->c = 3;
    size->z = 1;
    if (errno == 0 && *s) { // Read optional third and fourth integers
        size->z = apr_strtoi64(s, &s, 0);
        if (*s)
            size->c = apr_strtoi64(s, &s, 0);
    } // Raster size is 4 params max
    if (errno || *s)
        return " incorrect format";
    return NULL;
}

static const char *ConfigRaster(apr_pool_t *p, apr_table_t *kvp, struct TiledRaster &raster)
{
    const char *line;
    line = apr_table_get(kvp, "Size");
    if (!line)
        return "Size directive is mandatory";
    const char *err_message;
    err_message = get_xyzc_size(&(raster.size), line);
    if (err_message) return apr_pstrcat(p, "Size", err_message, NULL);
    // Optional page size, defaults to 512x512
    raster.pagesize.x = raster.pagesize.y = 512;
    line = apr_table_get(kvp, "PageSize");
    if (line) {
        err_message = get_xyzc_size(&(raster.pagesize), line);
        if (err_message) return apr_pstrcat(p, "PageSize", err_message, NULL);
    }

    // Optional data type, defaults to unsigned byte
    raster.datatype = GetDT(apr_table_get(kvp, "DataType"));

    line = apr_table_get(kvp, "SkippedLevels");
    if (line)
        raster.skip = int(apr_atoi64(line));

    // Default projection is WM, meaning web mercator
    line = apr_table_get(kvp, "Projection");
    raster.projection = line ? apr_pstrdup(p, line) : "WM";

    // Bounding box: minx, miny, maxx, maxy
    raster.bbox.xmin = raster.bbox.ymin = 0.0;
    raster.bbox.xmax = raster.bbox.ymax = 1.0;
    line = apr_table_get(kvp, "BoundingBox");
    if (line)
        err_message = getbbox(line, &raster.bbox);
    if (err_message)
        return apr_pstrcat(p, "BoundingBox", err_message, NULL);

    init_rsets(p, raster);

    return NULL;
}

// Returns a table read from a file, or NULL and an error message
static apr_table_t *read_pKVP_from_file(apr_pool_t *pool, const char *fname, char **err_message)

{
    *err_message = NULL;
    ap_configfile_t *cfg_file;
    apr_status_t s = ap_pcfg_openfile(&cfg_file, pool, fname);

    if (APR_SUCCESS != s) { // %pm means print status error string
        *err_message = apr_psprintf(pool, " %s - %pm", fname, &s);
        return NULL;
    }

    char buffer[MAX_STRING_LEN];
    apr_table_t *table = apr_table_make(pool, 8);
    // This can return ENOSPC if lines are too long
    while (APR_SUCCESS == (s = ap_cfg_getline(buffer, MAX_STRING_LEN, cfg_file))) {
        if ((strlen(buffer) == 0) || buffer[0] == '#')
            continue;
        const char *value = buffer;
        char *key = ap_getword_white(pool, &value);
        apr_table_add(table, key, value);
    }

    ap_cfg_closefile(cfg_file);
    if (s == APR_ENOSPC) {
        *err_message = apr_psprintf(pool, "maximum line length of %d exceeded", MAX_STRING_LEN);
        return NULL;
    }

    return table;
}

static const char *read_config(cmd_parms *cmd, twms_conf *c, const char *src, const char *fname)
{
    char *err_message;
    const char *line;

    // Start with the source configuration
    apr_table_t *kvp = read_pKVP_from_file(cmd->temp_pool, src, &err_message);
    if (NULL == kvp) return err_message;

    err_message = const_cast<char*>(ConfigRaster(cmd->pool, kvp, c->inraster));
    if (err_message) return apr_pstrcat(cmd->pool, "Source ", err_message, NULL);

    // Then the real configuration file
    kvp = read_pKVP_from_file(cmd->temp_pool, fname, &err_message);
    if (NULL == kvp) return err_message;
    err_message = const_cast<char *>(ConfigRaster(cmd->pool, kvp, c->raster));
    if (err_message) return err_message;

}

// Allow for one or more RegExp guard
// One of them has to match if the request is to be considered
static const char *set_regexp(cmd_parms *cmd, twms_conf *c, const char *pattern)
{
    char *err_message = NULL;
    if (c->regexp == 0)
        c->regexp = apr_array_make(cmd->pool, 2, sizeof(ap_regex_t));
    ap_regex_t *m = (ap_regex_t *)apr_array_push(c->regexp);
    int error = ap_regcomp(m, pattern, 0);
    if (error) {
        int msize = 2048;
        err_message = (char *)apr_pcalloc(cmd->pool, msize);
        ap_regerror(error, m, err_message, msize);
        return apr_pstrcat(cmd->pool, "Reproject Regexp incorrect ", err_message, NULL);
    }
    return NULL;
}

static void *create_dir_config(apr_pool_t *p, char *path)
{
    twms_conf *c = reinterpret_cast<twms_conf *>(apr_pcalloc(p, sizeof(twms_conf)));
    c->doc_path = path;
    return c;
}

static void register_hooks(apr_pool_t *p) {

}

static const command_rec cmds[] = {
    AP_INIT_TAKE2(
    "tWMS_ConfigurationFile",
    (cmd_func)read_config, // Callback
    0, // Self-pass argument
    ACCESS_CONF, // availability
    "Source and output configuration files"
    ),

    AP_INIT_TAKE1(
    "tWMS_RegExp",
    (cmd_func)set_regexp,
    0, // Self-pass argument
    ACCESS_CONF, // availability
    "Regular expression that the URL has to match.  At least one is required."),

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
