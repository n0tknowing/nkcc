int main(void)
{
    const char *a = "\"hello\n\n\n\n\r\"";
    const char *b = "";
    const char *c = "helllo
        world";
    return a[0] == 'a';
}
