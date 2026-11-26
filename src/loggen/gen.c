#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "common.h"

#define LOG_DEF_HEADER "#ifndef LOG_DEF_H\n#define LOG_DEF_H\n#include \"common.h\"\n\n"
#define LOG_DEF_END "#endif"
#define EVENT_ID_START "typedef enum {\n\t"
#define EVENT_ID_END "EVENT_COUNT\n} event_id_t;\n\n"
#define EVENT_SEVERITY_START "static const level_t event_levels[EVENT_COUNT] = {\n\t"
#define CLOSING_BRACKET "\n};\n\n"
#define EVENT_STRING_START "static const char *event_formats[EVENT_COUNT] = {\n\t"
#define MAX_DEFS 100
#define DEBUG "DEBUG"
#define INFO "INFO"
#define WARN "WARN"
#define ERROR "ERROR"

/* Notes
    - Need to allow for comments to occur
    - Crashes if there is blank lines in .def 
    - Only supports ordinary messages i.e. Parser does not accept quotations within messages
    - Check for duplicates
    - Can optimize the for loop into a function when writing to file
*/

const char *severity_names[] = {
    "LVL_DEBUG",
    "LVL_INFO",
    "LVL_WARN",
    "LVL_ERROR"
};

int read_word(const char **definition, char *output, size_t output_size) {

    while (isspace((unsigned char)**definition)) {
        (*definition)++;
    }

    const char *start = *definition;

    while (**definition != '\0' && !isspace((unsigned char)**definition)) {
        (*definition)++;
    }

    size_t length = *definition - start;

    if (length == 0 || length >= output_size) {
        return -1;
    }

    memcpy(output, start, length);
    output[length] = '\0';

    return 0;
}

level_t find_severity (const char *severity) {

     if (strcmp(severity, DEBUG) == 0) {
        return LVL_DEBUG;
    }
    else if (strcmp(severity, INFO) == 0) {
        return LVL_INFO;
    }
    else if (strcmp(severity, WARN) == 0) {
        return LVL_WARN;
    }
    else if (strcmp(severity, ERROR) == 0) {
        return LVL_ERROR;
    }
    else {
        return LVL_INVALID;
    }
}

/* parse a given line of a definition */
int parse_def (const char *definition, char *event_ID, size_t ID_size, level_t *event_severity, char *event_msg, size_t msg_size) {
    
    const char *p = definition;
    char severity[50];

    /* Save event ID */
    if (read_word(&p, event_ID, ID_size) != 0) {
        return -1;
    }

    /* Save severity */
    if (read_word(&p, severity, sizeof(severity)) != 0) {
        return -1;
    }

    /* Get severity level */
    *event_severity = find_severity(severity);

    if (*event_severity == LVL_INVALID) {
        return -1;
    }

    while (isspace((unsigned char)*p)) {
        p++;
    }

    /* Check that message begins with quotation marks */
    if (*p != '"') {
        return -1;
    }

    p++;

    /* Find the closing quotation mark */
    const char *start = p;

    while (*p != '"' && *p != '\0' && *p != '\n') {
        p++;
    }

    /* Check that a closing quotation mark exists */
    if (*p != '"') {
        return -1;
    }

    size_t length = p - start;

    if (length >= msg_size || length == 0) {
        return -1;
    }

    memcpy(event_msg, start, length);
    event_msg[length] = '\0';

    /* Move past closing quotation mark */
    p++;

    while (isspace((unsigned char)*p)) {
        p++;
    }

    if (*p != '\0') {
        return -1;
    }

    return 0;
}

int main () {

    FILE *log_file;
    FILE *def_file = fopen(EVENTS_PATH, "r");

    if (def_file == NULL) {
        perror("open file");
        return EXIT_FAILURE;
    }

    char line[512];
    char event_ID[MAX_DEFS][64];
    char event_msg[MAX_DEFS][512];
    level_t event_severity[MAX_DEFS];
    int event_count = 0;


    
    /* Transform data from file into appropraited defs */
    while (fgets(line, sizeof(line), def_file) != NULL)
    {
        if (event_count >= MAX_DEFS)
        {
            printf("Events is greater than maximum number of definitions");
            fclose(def_file);
            return EXIT_FAILURE;

        }
        
        /* transform line into agreed format  */
        if (parse_def(line, event_ID[event_count], sizeof(event_ID[event_count]), &event_severity[event_count], event_msg[event_count], sizeof(event_msg[event_count])) != 0) {
            printf("Invalid event definition: %s", line);
            fclose(def_file);
            return EXIT_FAILURE;
        }

        event_count ++;
    }

    if (ferror(def_file)) {
        perror("Error reading definitions file");
        fclose(def_file);
        return EXIT_FAILURE;
    }
    
    if (event_count == 0) {
        printf("Error: No event definitions found in %s\n", EVENTS_PATH);
        fclose(def_file);
        return EXIT_FAILURE;
    }

    /* Generate file log_h */
    log_file = fopen("log_defs.h", "w");

    if (log_file == NULL) {
        perror("create file");
        fclose(def_file);
        return EXIT_FAILURE;
    }

    /* Initialize file header for .h*/
    fprintf(log_file, "%s", LOG_DEF_HEADER);


    /* write to the log def file */
    /* Write event_ID */
    fprintf(log_file, "%s", EVENT_ID_START);

    for (int i = 0; i < event_count; i++) {
        fprintf(log_file, "%s,\n\t", event_ID[i]);
    }

    fprintf(log_file, "%s", EVENT_ID_END);

    /* Write event_severity */
    fprintf(log_file, "%s", EVENT_SEVERITY_START);

    for (int i = 0; i < event_count; i++) {
        fprintf(log_file, "[%s] = ", event_ID[i]);
        fprintf(log_file, "%s,\n\t", severity_names[event_severity[i]]);
    }

    fprintf(log_file, "%s", CLOSING_BRACKET);

    /* Write event_string */
    fprintf(log_file, "%s", EVENT_STRING_START);

    for (int i = 0; i < event_count; i++) {
        fprintf(log_file, "[%s] = ", event_ID[i]);
        fprintf(log_file, "\"%s\",\n\t", event_msg[i]);
    }

    fprintf(log_file, "%s", CLOSING_BRACKET);


    fprintf(log_file, "%s", LOG_DEF_END);

    fclose(log_file);
    fclose(def_file);

    return 0;
}