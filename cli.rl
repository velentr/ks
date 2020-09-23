#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ks.h"

%%{
	machine cli;

	write data noerror nofinal;

	action add {
		cfg->cmd = CMD_ADD;
	}

	action cat {
		cfg->cmd = CMD_CAT;
	}

	action categories {
		cfg->cmd = CMD_CATEGORIES;
	}

	action category {
		cfg->category = arg + 1;
	}

	action database {
		cfg->database = arg;
	}

	action file {
		cfg->file = arg;
	}

	action id {
		cfg->id = atoi(arg);
	}

	action init {
		cfg->cmd = CMD_INIT;
	}

	action mod {
		cfg->cmd = CMD_MOD;
	}

	action rm {
		cfg->cmd = CMD_RM;
	}

	action show {
		cfg->cmd = CMD_SHOW;
	}

	action tag {
		struct tag *t;

		t = malloc(sizeof(*t));
		if (t == NULL)
			err(EXIT_FAILURE, "malloc");
		t->next = cfg->tags;
		t->label = arg + 1;
		cfg->tags = t;
	}

	action title {
		cfg->title = arg;
	}

	global_option =
		( ("--database\0" | "-d\0") [^\0]+ %database '\0' );

	category = ( '@' [^\0]* %category '\0' );

	id = ( [0-9]+ %id '\0' );

	tag = ( '+' [^\0]+ %tag '\0' );

	title = ( ("--title\0" | "-t\0") [^\0]+ %title '\0' );

	add_option =
		  category
		| ( ("--file\0" | "-f\0") [^\0]+ %file '\0' )
		| tag
		| title;

	cat_option = id;

	mod_option =
		  category
		| id
		| tag
		| title;

	rm_option = id;

	show_option =
		  category
		| id;

	command =
		  ( "init" %init '\0' ( global_option )* )
		| ( "add" %add '\0' ( add_option | global_option )* )
		| ( "cat" %cat '\0' ( cat_option | global_option )* )
		| ( "categories" %categories '\0' ( global_option )* )
		| ( ("mod" | "modify") %mod '\0' ( mod_option | global_option )* )
		| ( "rm" %rm '\0' ( rm_option | global_option )* )
		| ( "show" %show '\0' ( show_option | global_option )* );

	main := ( global_option )* command;
}%%

void cli_parse(int argc, const char *argv[], struct config *cfg)
{
	const char *arg;
	const char *p;
	const char *pe;
	int i;
	int cs;

	%% write init;

	for (i = 1; i < argc; i++) {
		arg = argv[i];
		p = arg;
		pe = p + strlen(arg) + 1;

		%% write exec;

		if (cs == %%{ write error; }%%)
			errx(EXIT_FAILURE, "failed parsing argument: %s", arg);
	}
}
