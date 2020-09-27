#define _GNU_SOURCE

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <sqlite3.h>

#include "ks.h"

#define VERSION_MAJOR	0
#define VERSION_MINOR	0
#define VERSION_PATCH	0

#define _MAKESTR(s) #s
#define MAKESTR(s) _MAKESTR(s)

struct dynstr {
	struct dynstr *next;
	char data[];
};

struct stmt {
	struct stmt *next;
	sqlite3_stmt *stmt;
};

struct row {
	struct row *next;
	struct row *mnext;
	const char *title;
	const char *category;
	struct tag *tags;
	int id;
};

struct mem {
	struct sqlite3 *db;
	struct dynstr *s;
	struct stmt *stmts;
	struct tag *tags;
	struct row *rows;
};

static struct mem m;

static struct row *ks_row()
{
	struct row *r;

	r = malloc(sizeof(*r));
	if (r == NULL)
		err(EXIT_FAILURE, "malloc");
	r->mnext = m.rows;
	m.rows = r;

	return r;
}

struct tag *ks_tag(void)
{
	struct tag *t;

	t = malloc(sizeof(*t));
	if (t == NULL)
		err(EXIT_FAILURE, "malloc");
	t->mnext = m.tags;
	m.tags = t;

	return t;
}

static char *ks_stralloc(size_t len)
{
	struct dynstr *ds;

	ds = malloc(sizeof(*ds) + len + 1);
	if (ds == NULL)
		err(EXIT_FAILURE, "malloc(%lu)", len);
	ds->next = m.s;
	m.s = ds;

	return ds->data;
}

static char *ks_strdup(const char *s)
{
	char *r;

	r = ks_stralloc(strlen(s));
	strcpy(r, s);

	return r;
}

static sqlite3_stmt *ks_prepare(const char *sql)
{
	struct stmt *stmt;
	int rc;

	stmt = malloc(sizeof(*stmt));
	if (stmt == NULL)
		err(EXIT_FAILURE, "malloc");

	stmt->next = m.stmts;
	m.stmts = stmt;
	stmt->stmt = NULL;

	rc = sqlite3_prepare_v2(m.db, sql, -1, &stmt->stmt, NULL);
	if (rc != SQLITE_OK)
		errx(EXIT_FAILURE, "can't prepare statement: %s",
				sqlite3_errmsg(m.db));

	return stmt->stmt;
}

static sqlite3 *ks_open(const char *path)
{
	sqlite3 *db;
	int rc;

	rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL);
	m.db = db;
	if (rc != SQLITE_OK)
		errx(EXIT_FAILURE, "can't open %s: %s", path,
				sqlite3_errmsg(db));

	return db;
}

static void ks_begin(sqlite3 *db)
{
	int rc;

	rc = sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
	if (rc != SQLITE_OK)
		errx(EXIT_FAILURE, "failed to begin transaction: %s",
				sqlite3_errmsg(db));
}

static void ks_end(sqlite3 *db)
{
	int rc;

	rc = sqlite3_exec(db, "END;", NULL, NULL, NULL);
	if (rc != SQLITE_OK)
		errx(EXIT_FAILURE, "failed to commit transaction: %s",
				sqlite3_errmsg(db));
}

enum binding_t {
	BINDING_NULL,
	BINDING_INTEGER,
	BINDING_TEXT,
	BINDING_BLOB,
};

struct binding {
	union {
		int integer;
		const char *text;
		struct {
			void *data;
			int len;
		} blob;
	} value;
	enum binding_t type;
};

static void ks_bind(sqlite3 *db, sqlite3_stmt *stmt, struct binding *bindings,
		int nbindings)
{
	int i;
	int rc;

	for (i = 1; i <= nbindings; i++) {
		struct binding *b = &bindings[i-1];
		switch (b->type) {
		case BINDING_NULL:
			rc = sqlite3_bind_null(stmt, i);
			break;
		case BINDING_INTEGER:
			rc = sqlite3_bind_int(stmt, i, b->value.integer);
			break;
		case BINDING_TEXT:
			rc = sqlite3_bind_text(stmt, i, b->value.text, -1,
					SQLITE_STATIC);
			break;
		case BINDING_BLOB:
			rc = sqlite3_bind_blob(stmt, i, b->value.blob.data,
					b->value.blob.len, NULL);
			break;
		default:
			errx(EXIT_FAILURE, "invalid binding type: %d", b->type);
			break;
		}

		if (rc != 0)
			errx(EXIT_FAILURE, "failed binding: %s",
					sqlite3_errmsg(db));
	}
}

static void ks_run(sqlite3 *db, sqlite3_stmt *stmt,
		void (*cb)(sqlite3 *, sqlite3_stmt *, void *), void *arg)
{
	int rc;

	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		if (cb != NULL)
			cb(db, stmt, arg);
	}

	if (rc != SQLITE_DONE)
		errx(EXIT_FAILURE, "statement failed: %s", sqlite3_errmsg(db));
}

static void ks_sql(sqlite3 *db, const char *sql, struct binding *bindings,
		int nbindings, void (*cb)(sqlite3 *, sqlite3_stmt *, void *),
		void *arg)
{
	sqlite3_stmt *stmt;

	stmt = ks_prepare(sql);

	if (bindings != NULL)
		ks_bind(db, stmt, bindings, nbindings);

	ks_run(db, stmt, cb, arg);
}

static void ks_storeint(sqlite3 *db, sqlite3_stmt *stmt, void *_n)
{
	int *n = _n;

	(void)db;

	*n = sqlite3_column_int(stmt, 0);
}

static int ks_getcid(sqlite3 *db, const char *category)
{
	struct binding b = {
		.type = BINDING_TEXT,
		.value = {.text = category},
	};
	const char *sql = "SELECT cid FROM categories WHERE cname = ?;";
	int cid = -1;

	ks_sql(db, sql, &b, 1, ks_storeint, &cid);
	return cid;
}

static int ks_create_category(sqlite3 *db, const char *category)
{
	struct binding b = {
		.type = BINDING_TEXT,
		.value = {.text = category},
	};
	const char *sql = "INSERT INTO categories (cname) VALUES (?);";

	ks_sql(db, sql, &b, 1, NULL, NULL);

	return sqlite3_last_insert_rowid(db);
}

static int ks_cid(sqlite3 *db, const char *category)
{
	int cid;

	cid = ks_getcid(db, category);
	if (cid >= 0)
		return cid;

	return ks_create_category(db, category);
}

static void *ks_openfile(const char *filename, int *datalen)
{
	struct stat sb;
	void *buf;
	int fd;
	int rc;

	if (filename == NULL) {
		*datalen = 0;
		return NULL;
	}

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, "can't open '%s'", filename);

	rc = fstat(fd, &sb);
	if (rc < 0)
		err(EXIT_FAILURE, "can't stat '%s'", filename);

	*datalen = sb.st_size;
	if (sb.st_size == 0)
		return NULL;

	buf = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (buf == MAP_FAILED)
		err(EXIT_FAILURE, "can't read '%s'", filename);

	return buf;
}

static int ks_tid(sqlite3 *db, const char *label)
{
	struct binding b = {
		.type = BINDING_TEXT,
		.value = {.text = label},
	};
	const char *sql = "SELECT (tid) FROM tags WHERE label = ?;";
	const char *insql = "INSERT INTO tags (label) VALUES (?);";
	int tid = -1;

	ks_sql(db, sql, &b, 1, ks_storeint, &tid);

	if (tid >= 0)
		return tid;

	ks_sql(db, insql, &b, 1, NULL, NULL);

	return sqlite3_last_insert_rowid(db);
}

static void ks_inserttag(sqlite3 *db, int id, const char *label)
{
	struct binding b[] = {
		{
			.type = BINDING_INTEGER,
			.value = {.integer = id},
		}, {
			.type = BINDING_INTEGER,
		}
	};
	const char *sql = "INSERT INTO doctag (id, tid) VALUES (?, ?);";
	b[1].value.integer = ks_tid(db, label);
	ks_sql(db, sql, b, 2, NULL, NULL);
}

static void ks_add(const struct config *cfg)
{
	struct binding b[] = {
		{
			.type = BINDING_TEXT,
			.value = {.text = cfg->title},
		}, {
			.type = BINDING_INTEGER,
		}, {
			.type = BINDING_BLOB,
		}
	};
	const char *sql =
		"INSERT INTO documents (title, cid, data) VALUES (?, ?, ?);";
	sqlite3 *db;
	struct tag *t;
	int datalen;
	int id;

	if (cfg->title == NULL)
		errx(EXIT_FAILURE, "title is required when adding a document");

	db = ks_open(cfg->database);
	ks_begin(db);

	if (cfg->category == NULL)
		b[1].value.integer = ks_cid(db, "");
	else
		b[1].value.integer = ks_cid(db, cfg->category);

	b[2].value.blob.data = ks_openfile(cfg->file, &datalen);
	if (datalen == 0)
		b[2].type = BINDING_NULL;
	else
		b[2].value.blob.len = datalen;

	ks_sql(db, sql, b, 3, NULL, NULL);

	id = sqlite3_last_insert_rowid(db);
	for (t = cfg->tags; t != NULL; t = t->next)
		ks_inserttag(db, id, t->label);

	ks_end(db);
}

static void ks_printblob(sqlite3 *db, sqlite3_stmt *stmt, void *arg)
{
	const void *blob;
	int len;

	(void)db;
	(void)arg;

	blob = sqlite3_column_blob(stmt, 0);
	if (blob == NULL)
		return;
	len = sqlite3_column_bytes(stmt, 0);
	fwrite(blob, 1, len, stdout);
}

static void ks_cat(const struct config *cfg)
{
	struct binding b = {
		.type = BINDING_INTEGER,
		.value = {.integer = cfg->id},
	};
	sqlite3 *db;
	const char *sql = "SELECT (data) FROM documents WHERE id = ?;";

	if (cfg->id < 0)
		errx(EXIT_FAILURE, "cat command requires an id");

	db = ks_open(cfg->database);
	ks_sql(db, sql, &b, 1, ks_printblob, NULL);
}

static void ks_printcname(sqlite3 *db, sqlite3_stmt *stmt, void *arg)
{
	const char *cname;

	(void)db;
	(void)arg;

	cname = (void *)sqlite3_column_text(stmt, 0);
	printf("%s\n", cname);
}

static void ks_categories(const struct config *cfg)
{
	const char *sql = "SELECT (cname) FROM categories;";
	sqlite3 *db;

	db = ks_open(cfg->database);
	ks_sql(db, sql, NULL, 0, ks_printcname, NULL);
}

static void ks_init(const struct config *cfg)
{
	const char *sql =
		"BEGIN TRANSACTION;"
		"CREATE TABLE categories ("
			"cid INTEGER PRIMARY KEY,"
			"cname TEXT"
		");"
		"CREATE TABLE documents ("
			"id INTEGER PRIMARY KEY,"
			"title TEXT,"
			"cid INTEGER,"
			"data BLOB,"
			"FOREIGN KEY (cid) REFERENCES categories(cid)"
		");"
		"CREATE TABLE tags ("
			"tid INTEGER PRIMARY KEY,"
			"label TEXT"
		");"
		"CREATE TABLE doctag ("
			"id INTEGER,"
			"tid INTEGER,"
			"FOREIGN KEY (id) REFERENCES documents(id),"
			"FOREIGN KEY (tid) REFERENCES tags(tid)"
		");"
		"CREATE TABLE version (v INTEGER);"
		"INSERT INTO version (v) VALUES (" MAKESTR(VERSION_MAJOR) ");"
		"END;";
	sqlite3 *db;
	int rc;

	rc = sqlite3_open_v2(cfg->database, &db,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	m.db = db;
	if (rc != SQLITE_OK)
		errx(EXIT_FAILURE, "can't create database %s: %s",
				cfg->database, sqlite3_errmsg(db));

	rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
	if (rc != SQLITE_OK)
		errx(EXIT_FAILURE, "table creation failed: %s",
				sqlite3_errmsg(db));
}

static void ks_settitle(sqlite3 *db, int id, const char *title)
{
	struct binding b[] = {
		{
			.type = BINDING_TEXT,
			.value = {.text = title},
		}, {
			.type = BINDING_INTEGER,
			.value = {.integer = id},
		}
	};
	const char *sql = "UPDATE documents SET title = ? WHERE id = ?;";

	ks_sql(db, sql, b, 2, NULL, NULL);
}

static void ks_setcategory(sqlite3 *db, int id, const char *category)
{
	struct binding b[] = {
		{
			.type = BINDING_INTEGER,
		}, {
			.type = BINDING_INTEGER,
			.value = {.integer = id},
		}
	};
	const char *sql = "UPDATE documents SET cid = ? WHERE id = ?;";

	b[0].value.integer = ks_cid(db, category);
	ks_sql(db, sql, b, 2, NULL, NULL);
}

static void ks_mod(const struct config *cfg)
{
	sqlite3 *db;
	struct tag *t;

	if (cfg->id < 0)
		errx(EXIT_FAILURE, "mod command requires an id");

	db = ks_open(cfg->database);
	ks_begin(db);

	if (cfg->category != NULL)
		ks_setcategory(db, cfg->id, cfg->category);

	if (cfg->title != NULL)
		ks_settitle(db, cfg->id, cfg->title);

	for (t = cfg->tags; t != NULL; t = t->next)
		ks_inserttag(db, cfg->id, t->label);

	ks_end(db);
}

static void ks_rm(const struct config *cfg)
{
	struct binding b = {
		.type = BINDING_INTEGER,
		.value = {.integer = cfg->id}
	};
	const char *sql = "DELETE FROM documents WHERE id = ?;";
	sqlite3 *db;

	if (cfg->id < 0)
		errx(EXIT_FAILURE, "id required for rm command");

	db = ks_open(cfg->database);

	ks_sql(db, sql, &b, 1, NULL, NULL);
}

struct table {
	struct row *rows;

	size_t titlewidth;
	size_t categorywidth;
	size_t idwidth;
	size_t tagwidth;
	int idcap;
};

static void ks_collecttag(sqlite3 *db, sqlite3_stmt *stmt, void *_tags)
{
	struct tag **tags = _tags;
	struct tag *t;

	(void)db;

	t = ks_tag();
	t->label = ks_strdup((const char *)sqlite3_column_text(stmt, 0));
	t->next = *tags;
	*tags = t;
}

static struct tag *ks_gettags(sqlite3 *db, int id)
{
	struct binding b = {
		.type = BINDING_INTEGER,
		.value = {.integer = id},
	};
	const char *sql =
		"SELECT label "
		"FROM doctag INNER JOIN tags "
			"ON doctag.tid = tags.tid "
		"WHERE id = ?;";
	struct tag *result = NULL;

	ks_sql(db, sql, &b, 1, ks_collecttag, &result);

	return result;
}

static size_t ks_gettagwidth(struct tag *t)
{
	size_t width = 0;
	for (; t != NULL; t = t->next)
		width += strlen(t->label) + 1;
	return width;
}

static void ks_saverow(sqlite3 *db, sqlite3_stmt *stmt, void *_tbl)
{
	struct table *tbl = _tbl;
	struct row *r;
	const char *title;
	const char *category;
	size_t tagwidth;
	int id;

	id = sqlite3_column_int(stmt, 0);
	title = (void *)sqlite3_column_text(stmt, 1);
	category = (void *)sqlite3_column_text(stmt, 2);

	if (id >= tbl->idcap) {
		tbl->idcap *= 10;
		tbl->idwidth++;
	}

	if (strlen(title) > tbl->titlewidth)
		tbl->titlewidth = strlen(title);

	if (strlen(category) > tbl->categorywidth)
		tbl->categorywidth = strlen(category);

	r = ks_row();
	r->next = tbl->rows;
	r->title = ks_strdup(title);
	r->category = ks_strdup(category);
	r->id = id;
	r->tags = ks_gettags(db, id);
	tbl->rows = r;

	tagwidth = ks_gettagwidth(r->tags);
	if (tagwidth > tbl->tagwidth)
		tbl->tagwidth = tagwidth;
}

static void ks_printheader(const struct table *tbl)
{
	size_t i;

	printf("\e[4mID  ");
	for (i = 2; i < tbl->idwidth; i++)
		printf(" ");
	printf("Category  ");
	for (i = strlen("Category"); i < tbl->categorywidth; i++)
		printf(" ");
	printf("Title  ");
	for (i = strlen("Title"); i < tbl->titlewidth; i++)
		printf(" ");
	printf("Tags");
	for (i = strlen("Tags"); i < tbl->tagwidth; i++)
		printf(" ");
	printf("\n\e[0m");
}

static void ks_printrow(const struct table *tbl, const struct row *r)
{
	size_t i;
	int cap;
	struct tag *t;

	for (cap = tbl->idcap / 10; r->id < cap; cap /= 10)
		printf(" ");
	printf("%d  ", r->id);
	printf("%s  ", r->category);
	for (i = strlen(r->category); i < tbl->categorywidth; i++)
		printf(" ");
	printf("%s  ", r->title);
	for (i = strlen(r->title); i < tbl->titlewidth; i++)
		printf(" ");
	for (t = r->tags; t != NULL; t = t->next)
		printf("%s ", t->label);
	printf("\n");
}

static void ks_showtags(const struct config *cfg, struct table *tbl)
{
	struct binding b[] = {
		{
			.type = BINDING_TEXT,
			.value = {.text = cfg->category}
		}, {
			.type = BINDING_TEXT,
			.value = {.text = cfg->tags->label}
		}
	};
	sqlite3 *db;
	const char *sql =
		"SELECT documents.id, title, cname "
		"FROM documents JOIN categories "
			"ON documents.cid = categories.cid "
		"JOIN doctag ON documents.id = doctag.id "
		"JOIN tags ON doctag.tid = tags.tid "
		"WHERE cname LIKE ? AND label LIKE ?;";

	if (cfg->category == NULL)
		b[0].value.text = "%";

	if (cfg->tags->next != NULL)
		errx(EXIT_FAILURE, "can only filter on a single tag");

	db = ks_open(cfg->database);
	ks_sql(db, sql, b, 2, ks_saverow, tbl);
}

static void ks_showcategory(const struct config *cfg, struct table *tbl)
{
	struct binding b = {
		.type = BINDING_TEXT,
		.value = {.text = cfg->category}
	};
	sqlite3 *db;
	const char *sql =
		"SELECT id, title, cname "
		"FROM documents INNER JOIN categories "
			"ON documents.cid = categories.cid "
		"WHERE cname LIKE ?;";


	if (cfg->tags != NULL) {
		ks_showtags(cfg, tbl);
	} else {
		if (cfg->category == NULL)
			b.value.text = "%";

		db = ks_open(cfg->database);
		ks_sql(db, sql, &b, 1, ks_saverow, tbl);
	}
}

static void ks_showid(const struct config *cfg, struct table *tbl)
{
	struct binding b = {
		.type = BINDING_INTEGER,
		.value = {.integer = cfg->id}
	};
	sqlite3 *db;
	const char *sql =
		"SELECT id, title, cname "
		"FROM documents INNER JOIN categories "
			"ON documents.cid = categories.cid "
		"WHERE id = ?;";

	db = ks_open(cfg->database);
	ks_sql(db, sql, &b, 1, ks_saverow, tbl);
}

static void ks_show(const struct config *cfg)
{
	struct table tbl = {
		.rows = NULL,
		.idwidth = 2,
		.idcap = 100,
		.titlewidth = strlen("Title"),
		.categorywidth = strlen("Category"),
		.tagwidth = strlen("Tags"),
	};
	struct row *r;

	if (cfg->id < 0)
		ks_showcategory(cfg, &tbl);
	else
		ks_showid(cfg, &tbl);

	if (!cfg->noheader)
		ks_printheader(&tbl);
	for (r = tbl.rows; r != NULL; r = r->next)
		ks_printrow(&tbl, r);
}

static char *ks_home(const char *name)
{
	const char *home;
	char *result;
	int len;

	home = getenv("HOME");
	if (home == NULL)
		errx(EXIT_FAILURE, "can't find HOME directory?");

	len = snprintf(NULL, 0, "%s/%s", home, name);
	if (len < 0)
		errx(EXIT_FAILURE, "can't compute default database name");
	result = ks_stralloc(len);
	snprintf(result, len + 1, "%s/%s", home, name);

	return result;
}

static void ks_cleanup(void)
{
	struct dynstr *s, *n;
	struct stmt *stmt, *nstmt;
	struct row *r, *nr;
	struct tag *t, *nt;

	for (t = m.tags; t != NULL; t = nt) {
		nt = t->mnext;
		free(t);
	}

	for (r = m.rows; r != NULL; r = nr) {
		nr = r->mnext;
		free(r);
	}

	for (s = m.s; s != NULL; s = n) {
		n = s->next;
		free(s);
	}

	for (stmt = m.stmts; stmt != NULL; stmt = nstmt) {
		nstmt = stmt->next;
		if (stmt->stmt != NULL)
			sqlite3_finalize(stmt->stmt);
		free(stmt);
	}

	if (m.db != NULL)
		sqlite3_close(m.db);
}

static void ks_help(void)
{
	printf("usage: %s [-d | --database <path>] <command> [<args>]\n\n",
			program_invocation_short_name);
	printf("commands:\n");
	printf("  add\t\tadd a new document to the database\n");
	printf("  cat\t\tread the file contents of a document in the database\n");
	printf("  categories\tlist all categories in the database\n");
	printf("  help\t\tprint this usage message\n");
	printf("  init\t\tcreate a new document database\n");
	printf("  mod\t\tmodify an existing document's metadata\n");
	printf("  rm\t\tremove a document from the database\n");
	printf("  show\t\tprint document metadata from the database\n");
	printf("  version\tprint the cli tool's version\n");
	printf("\nsee ks(1) for detailed usage of each command\n");
}

static void ks_dbversion(const struct config *cfg)
{
	const char *sql = "SELECT v FROM version;";
	sqlite3 *db;
	int v;

	db = ks_open(cfg->database);
	ks_sql(db, sql, NULL, 0, ks_storeint, &v);
	printf("%s database version %d\n", program_invocation_short_name, v);
}

static void ks_version(const struct config *cfg)
{
	if (cfg->dbversion)
		ks_dbversion(cfg);
	else
		printf("%s version %d.%d.%d\n", program_invocation_short_name,
				VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
}

int main(int argc, const char *argv[])
{
	struct config cfg = {
		.category = NULL,
		.cmd  = CMD_NONE,
		.dbversion = 0,
		.file = NULL,
		.id = -1,
		.noheader = 0,
		.tags = NULL,
		.title = NULL,
	};
	atexit(ks_cleanup);

	cfg.database = ks_home(".ksdb");

	cli_parse(argc, argv, &cfg);

	switch (cfg.cmd) {
	case CMD_ADD:
		ks_add(&cfg);
		break;
	case CMD_CAT:
		ks_cat(&cfg);
		break;
	case CMD_CATEGORIES:
		ks_categories(&cfg);
		break;
	case CMD_INIT:
		ks_init(&cfg);
		break;
	case CMD_MOD:
		ks_mod(&cfg);
		break;
	case CMD_RM:
		ks_rm(&cfg);
		break;
	case CMD_SHOW:
		ks_show(&cfg);
		break;
	case CMD_VERSION:
		ks_version(&cfg);
		break;
	case CMD_HELP:
	default:
		ks_help();
	}

	return EXIT_SUCCESS;
}
