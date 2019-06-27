/**
 * @file mediafile.c Dash Performance client -- mediafile
 *
 * Copyright (C) 2019 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include "dashperf.h"


#define DEBUG_MODULE "dashperf"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


static void mediafile_destructor(void *data)
{
	struct mediafile *mf = data;

	list_unlink(&mf->le);
	mem_deref(mf->filename);
}


int mediafile_new(struct list *lst, const char *filename, double duration)
{
	struct mediafile *mf;
	int err;

	if (!lst || !filename)
		return EINVAL;

	mf = mem_zalloc(sizeof(*mf), mediafile_destructor);
	if (!mf)
		return ENOMEM;

	err = str_dup(&mf->filename, filename);
	if (err)
		goto out;

	mf->duration = duration;

	list_append(lst, &mf->le, mf);

 out:
	if (err)
		mem_deref(mf);

	return err;
}
