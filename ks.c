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

static char *ks_strdup(const char *s)
{
	char *r;

	r = malloc(strlen(s) + 1);
	if (r == NULL)
		err(EXIT_FAILURE, "malloc(%lu)", strlen(s));

	strcpy(r, s);

	return r;
}

static sqlite3 *ks_open(const char *path)
{
	sqlite3 *db;
	int rc;

	rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL);
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
	int rc;

	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		errx(EXIT_FAILURE, "failed to prepare statement: %s",
				sqlite3_errmsg(db));

	if (bindings != NULL)
		ks_bind(db, stmt, bindings, nbindings);

	ks_run(db, stmt, cb, arg);
}

static void ks_cid_store(sqlite3 *db, sqlite3_stmt *stmt, void *_cid)
{
	int *cid = _cid;
	(void)db;
	*cid = sqlite3_column_int(stmt, 0);
}

static int ks_getcid(sqlite3 *db, const char *category)
{
	struct binding b = {
		.type = BINDING_TEXT,
		.value = {.text = category},
	};
	const char *sql = "SELECT cid FROM categories WHERE cname = ?;";
	int cid = -1;

	ks_sql(db, sql, &b, 1, ks_cid_store, &cid);
	return cid;
}

static void ks_create_category(sqlite3 *db, const char *category)
{
	struct binding b = {
		.type = BINDING_TEXT,
		.value = {.text = category},
	};
	const char *sql = "INSERT INTO categories (cname) VALUES (?);";

	ks_sql(db, sql, &b, 1, NULL, NULL);
}

static int ks_cid(sqlite3 *db, const char *category)
{
	int cid;

	cid = ks_getcid(db, category);
	if (cid >= 0)
		return cid;

	ks_create_category(db, category);
	cid = ks_getcid(db, category);
	if (cid < 0)
		errx(EXIT_FAILURE, "can't find category '%s'?", category);

	return cid;
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

static void ks_storetid(sqlite3 *db, sqlite3_stmt *stmt, void *_tid)
{
	int *tid = _tid;

	(void)db;

	*tid = sqlite3_column_int(stmt, 0);
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

	ks_sql(db, sql, &b, 1, ks_storetid, &tid);

	if (tid >= 0)
		return tid;

	ks_sql(db, insql, &b, 1, NULL, NULL);

	return sqlite3_last_insert_rowid(db);
}

static void ks_add(const struct config *cfg)
{
	struct binding docb[] = {
		{
			.type = BINDING_TEXT,
			.value = {.text = cfg->title},
		}, {
			.type = BINDING_INTEGER,
		}, {
			.type = BINDING_BLOB,
		}
	};
	struct binding tagb[] = {
		{
			.type = BINDING_INTEGER,
		}, {
			.type = BINDING_INTEGER,
		}
	};
	const char *docsql =
		"INSERT INTO documents (title, cid, data) VALUES (?, ?, ?);";
	const char *tagsql = "INSERT INTO doctag (id, tid) VALUES (?, ?);";
	sqlite3 *db;
	struct tag *t;
	int datalen;

	if (cfg->title == NULL)
		errx(EXIT_FAILURE, "title is required when adding a document");

	db = ks_open(cfg->database);
	ks_begin(db);

	if (cfg->category == NULL)
		docb[1].value.integer = ks_cid(db, "");
	else
		docb[1].value.integer = ks_cid(db, cfg->category);

	docb[2].value.blob.data = ks_openfile(cfg->file, &datalen);
	if (datalen == 0)
		docb[2].type = BINDING_NULL;
	else
		docb[2].value.blob.len = datalen;

	ks_sql(db, docsql, docb, 3, NULL, NULL);

	tagb[0].value.integer = sqlite3_last_insert_rowid(db);
	for (t = cfg->tags; t != NULL; t = t->next) {
		tagb[1].value.integer = ks_tid(db, t->label);
		/* TODO we can save some time by cacheing the statement here */
		ks_sql(db, tagsql, tagb, 2, NULL, NULL);
	}

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
		"END;";
	sqlite3 *db;
	int rc;

	rc = sqlite3_open_v2(cfg->database, &db,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
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

	if (cfg->id < 0)
		errx(EXIT_FAILURE, "mod command requires an id");

	db = ks_open(cfg->database);
	ks_begin(db);

	if (cfg->category != NULL)
		ks_setcategory(db, cfg->id, cfg->category);

	if (cfg->title != NULL)
		ks_settitle(db, cfg->id, cfg->title);

	ks_end(db);
}

struct row {
	struct row *next;
	const char *title;
	const char *category;
	int id;
};

struct table {
	struct row *rows;

	size_t titlewidth;
	size_t categorywidth;
	size_t idwidth;
	int idcap;
};

static void ks_saverow(sqlite3 *db, sqlite3_stmt *stmt, void *_tbl)
{
	struct table *tbl = _tbl;
	struct row *r;
	const char *title;
	const char *category;
	int id;

	(void)db;

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

	r = malloc(sizeof(*r));
	if (r == NULL)
		err(EXIT_FAILURE, "malloc(%lu)", sizeof(*r));

	r->next = tbl->rows;
	r->title = ks_strdup(title);
	r->category = ks_strdup(category);
	r->id = id;
	tbl->rows = r;
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
	printf("Title");
	for (i = strlen("Title"); i < tbl->titlewidth; i++)
		printf(" ");
	printf("\n\e[0m");
}

static void ks_printrow(const struct table *tbl, const struct row *r)
{
	size_t i;
	int cap;

	for (cap = tbl->idcap / 10; r->id < cap; cap /= 10)
		printf(" ");
	printf("%d  ", r->id);
	printf("%s  ", r->category);
	for (i = strlen(r->category); i < tbl->categorywidth; i++)
		printf(" ");
	printf("%s", r->title);
	for (i = strlen(r->title); i < tbl->titlewidth; i++)
		printf(" ");
	printf("\n");
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

static void ks_show(const struct config *cfg)
{
	struct binding b = {
		.type = BINDING_TEXT,
		.value = {.text = cfg->category}
	};
	struct table tbl = {
		.rows = NULL,
		.idwidth = 2,
		.idcap = 100,
		.titlewidth = strlen("Title"),
		.categorywidth = strlen("Category"),
	};
	sqlite3 *db;
	const char *sql =
		"SELECT id, title, cname "
		"FROM documents INNER JOIN categories "
			"ON documents.cid = categories.cid "
		"WHERE cname LIKE ?;";
	struct row *r;

	if (cfg->category == NULL)
		b.value.text = "%";

	db = ks_open(cfg->database);
	ks_sql(db, sql, &b, 1, ks_saverow, &tbl);

	ks_printheader(&tbl);
	for (r = tbl.rows; r != NULL; r = r->next)
		ks_printrow(&tbl, r);
}

static char *ks_home(const char *name)
{
	const char *home;
	char *result;
	int rc;

	home = getenv("HOME");
	if (home == NULL)
		errx(EXIT_FAILURE, "can't find HOME directory?");

	rc = asprintf(&result, "%s/%s", home, name);
	if (rc < 0)
		errx(EXIT_FAILURE, "can't compute default database name");

	return result;
}

int main(int argc, const char *argv[])
{
	struct config cfg = {
		.category = NULL,
		.cmd  = CMD_NONE,
		.file = NULL,
		.id = -1,
		.tags = NULL,
		.title = NULL,
	};
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
	default:
		errx(EXIT_FAILURE, "unknown command: %d", cfg.cmd);
	}

	return EXIT_SUCCESS;
}