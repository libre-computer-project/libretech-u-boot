
int ini_parse(char *filestart, size_t filelen,
	int (*handler)(void *, char *, char *, char *),	void *user);
int ini_handler(void *user, char *section, char *name, char *value);
