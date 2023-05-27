int main(void)
{
    const char *z = "\x41\x20\x43\x20\x43ompiler";
    const char *a = "\"hello\n\n\n\n\r\"";
    const char *b = "";
    const char *c = "hello \
        world";
    return a[0] == 'a';
}
