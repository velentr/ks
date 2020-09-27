#ifndef KS_H_
#define KS_H_


enum command {
	CMD_NONE,
	CMD_ADD,
	CMD_CAT,
	CMD_CATEGORIES,
	CMD_HELP,
	CMD_INIT,
	CMD_MOD,
	CMD_RM,
	CMD_SHOW,
	CMD_VERSION,
};

struct tag {
	struct tag *next;
	struct tag *mnext;
	const char *label;
};

struct config {
	const char *category;
	const char *database;
	const char *file;
	const char *title;
	struct tag *tags;
	enum command cmd;
	int id;
	int noheader;
	int dbversion;
};

void cli_parse(int argc, const char *argv[], struct config *cfg);

struct tag *ks_tag();


#endif /* end of include guard: KS_H_ */
