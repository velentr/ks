#ifndef KS_H_
#define KS_H_


enum command {
	CMD_NONE,
	CMD_ADD,
	CMD_CAT,
	CMD_CATEGORIES,
	CMD_INIT,
	CMD_RM,
	CMD_SHOW,
};

struct config {
	const char *category;
	const char *database;
	const char *file;
	const char *title;
	enum command cmd;
	int id;
};

void cli_parse(int argc, const char *argv[], struct config *cfg);


#endif /* end of include guard: KS_H_ */
