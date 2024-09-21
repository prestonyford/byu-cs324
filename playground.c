#include <unistd.h>

int main() {
    char s1[] = { 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x0a }; // Hello
    char s2[] = { 0xe5, 0x8f, 0xb0, 0xe7, 0x81, 0xa3, 0x0a }; // Some Chinese characters
    write(STDOUT_FILENO, s1, 6);
    write(STDOUT_FILENO, s2, 7);
    return 0;
}