
#include "signer.h"
#include <assuan.h>
#include <stdarg.h>


int log_get_fd()
{
	return 5;
}

void
log_error( const char *fmt, ... )
{
    va_list arg_ptr ;

    va_start( arg_ptr, fmt ) ;
    vfprintf(stderr, fmt, arg_ptr );
    va_end(arg_ptr);
}

struct entry_parm_s {
  int lines;
  size_t size;
  char *buffer;
};

static AssuanError
getpin_cb (void *opaque, const void *buffer, size_t length)
{
  struct entry_parm_s *parm = opaque;

  /* we expect the pin to fit on one line */
  if (parm->lines || length >= parm->size)
    return ASSUAN_Too_Much_Data;

  /* fixme: we should make sure that the assuan buffer is allocated in
     secure memory or read the response byte by byte */
  memcpy(parm->buffer, buffer, length);
  parm->buffer[length] = 0;
  parm->lines++;
  return 0;
}

int ask_and_verify_pin_code(struct sc_pkcs15_card *p15card,
			    struct sc_pkcs15_pin_info *pinfo)
{
	int r;
	const char *argv[3];
	const char *pgmname = "/usr/local/bin/gpinentry";
	ASSUAN_CONTEXT ctx;
	char buf[500];
	char errtext[100];
	struct entry_parm_s parm;
	
	argv[0] = pgmname;
	argv[1] = NULL;
	
	r = assuan_pipe_connect(&ctx, pgmname, (char **) argv);
	if (r) {
		printf("Can't connect to the PIN entry module: %s\n",
		       assuan_strerror(r));
		goto err;
	}
	sprintf(buf, "SETDESC Enter PIN [%s] for digital signing  ", pinfo->com_attr.label);
	r = assuan_transact(ctx, buf, NULL, NULL, NULL, NULL);
	if (r) {
		printf("SETDESC: %s\n", assuan_strerror(r));
		goto err;
	}
	errtext[0] = 0;
	while (1) {
		if (errtext[0]) {
			sprintf(buf, "SETERROR %s", errtext);
			r = assuan_transact(ctx, buf, NULL, NULL, NULL, NULL);
			errtext[0] = 0;
		}
		parm.lines = 0;
		parm.size = sizeof(buf);
		parm.buffer = buf;
		r = assuan_transact(ctx, "GETPIN", getpin_cb, &parm, NULL, NULL);
		if (r == ASSUAN_Canceled) {
			assuan_pipe_disconnect(ctx);
			return -2;
		}
		if (r) {
			printf("GETPIN: %s\n", assuan_strerror(r));
			goto err;
		}
		r = strlen(buf);
		if (r < pinfo->min_length) {
			sprintf(errtext, "PIN code too short, min. %d digits", pinfo->min_length);
			continue;
		}
		if (r > pinfo->stored_length) {
			sprintf(errtext, "PIN code too long, max. %d digits", pinfo->stored_length);
			continue;
		}
		r = sc_pkcs15_verify_pin(p15card, pinfo, buf, strlen(buf));
		switch (r) {
		case SC_ERROR_PIN_CODE_INCORRECT:
			sprintf(errtext, "PIN code incorrect (%d %s left)",
			       pinfo->tries_left, pinfo->tries_left == 1 ?
			       "try" : "tries");
			break;
		case 0:
			break;
		default:
			goto err;
		}
		if (r == 0)
			break;
	}

	assuan_pipe_disconnect(ctx);	
	return 0;
err:	
	assuan_pipe_disconnect(ctx);
	return -1;
}
