#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    char *content_length_str = getenv("CONTENT_LENGTH");
    int content_length = 0;
    if (content_length_str != NULL) {
        content_length = atoi(content_length_str);
    }
    char *query_string = getenv("QUERY_STRING");

    char body[content_length + 1];
    read(STDIN_FILENO, body, content_length);
    body[content_length] = '\0';

    char response[1024];
    sprintf(response, "Hello CS324\nQuery string: %s\nRequest body: %s\n", query_string, body);

    printf("Content-Type: text/plain\r\n");
    printf("Content-Length: %ld\r\n\r\n", strlen(response));
    printf("%s", response);

    return 0;
}