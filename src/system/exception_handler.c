#include <stdio.h>
#include <stdarg.h>
#include "common/types.h"
#include "exception_handler.h"

#define OS_EXCEPTION_DSI                        2
#define OS_EXCEPTION_ISI                        3
#define OS_EXCEPTION_PROGRAM                    6

#include <coreinit/exception.h>
#include <coreinit/debug.h>

#define CPU_STACK_TRACE_DEPTH		8
#define __stringify(rn)				#rn

#define mfspr(_rn) \
({	register uint32_t _rval = 0; \
	asm volatile("mfspr %0," __stringify(_rn) \
	: "=r" (_rval));\
	_rval; \
})

typedef struct _framerec {
	struct _framerec *up;
	void *lr;
} frame_rec, *frame_rec_t;

static const char *exception_names[] = {
    "DSI",
    "ISI",
    "PROGRAM"
};

static const char exception_print_formats[16][45] = {
     "Exception type %s occurred!\n",                       // 0
     "GPR00 %08X GPR08 %08X GPR16 %08X GPR24 %08X\n",       // 1
     "GPR01 %08X GPR09 %08X GPR17 %08X GPR25 %08X\n",       // 2
     "GPR02 %08X GPR10 %08X GPR18 %08X GPR26 %08X\n",       // 3
     "GPR03 %08X GPR11 %08X GPR19 %08X GPR27 %08X\n",       // 4
     "GPR04 %08X GPR12 %08X GPR20 %08X GPR28 %08X\n",       // 5
     "GPR05 %08X GPR13 %08X GPR21 %08X GPR29 %08X\n",       // 6
     "GPR06 %08X GPR14 %08X GPR22 %08X GPR30 %08X\n",       // 7
     "GPR07 %08X GPR15 %08X GPR23 %08X GPR31 %08X\n",       // 8
     "LR    %08X SRR0  %08x SRR1  %08x\n",                  // 9
     "DAR   %08X DSISR %08X\n",                             // 10
     "\nSTACK DUMP:",                                       // 11
     " --> ",                                               // 12
      " -->\n",                                             // 13
      "\n",                                                 // 14
      "%p",                                                 // 15
};

// Bounded append into the fatal-error buffer; never writes past cap and always
// leaves the buffer NUL-terminated (vsnprintf guarantee).
static int addf(char *buf, int pos, int cap, const char *fmt, ...)
{
	if (pos < 0 || pos >= cap - 1)
		return pos < 0 ? cap - 1 : pos;
	va_list va; va_start(va, fmt);
	// The only callers pass entries of the fixed static format table at the
	// top of this file, so the non-literal format is by design here; the
	// table rows and their argument lists were reviewed by hand.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
	int n = vsnprintf(buf + pos, (size_t)(cap - pos), fmt, va);
#pragma GCC diagnostic pop
	va_end(va);
	if (n < 0)
		return cap - 1;
	return pos + n > cap - 1 ? cap - 1 : pos + n;
}

static unsigned char exception_cb(OSContext * context, unsigned char exception_type) {
    char buf[850];
    int pos = 0;
    /*
     * This part is mostly from libogc. Thanks to the devs over there.
     */
	pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[0], exception_names[exception_type]);
	pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[1], context->gpr[0], context->gpr[8], context->gpr[16], context->gpr[24]);
	pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[2], context->gpr[1], context->gpr[9], context->gpr[17], context->gpr[25]);
	pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[3], context->gpr[2], context->gpr[10], context->gpr[18], context->gpr[26]);
	pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[4], context->gpr[3], context->gpr[11], context->gpr[19], context->gpr[27]);
	pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[5], context->gpr[4], context->gpr[12], context->gpr[20], context->gpr[28]);
	pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[6], context->gpr[5], context->gpr[13], context->gpr[21], context->gpr[29]);
	pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[7], context->gpr[6], context->gpr[14], context->gpr[22], context->gpr[30]);
	pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[8], context->gpr[7], context->gpr[15], context->gpr[23], context->gpr[31]);
	pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[9], context->lr, context->srr0, context->srr1);

	//if(exception_type == OS_EXCEPTION_DSI) {
        pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[10], context->dar, context->dsisr);
	//}

    void *pc = (void*)context->srr0;
    void *lr = (void*)context->lr;
    void *r1 = (void*)context->gpr[1];
	register uint32_t i = 0;
	register frame_rec_t l,p = (frame_rec_t)lr;

	l = p;
	p = r1;
	if(!p)
        asm volatile("mr %0,%%r1" : "=r"(p));

	/* The frame chain comes from stack memory: after a corruption the links
	   are untrusted pointers. Dereferencing a bogus one inside the exception
	   handler would double-fault and lose the OSFatal report entirely, so
	   every link is validated (non-null, 8-byte aligned, inside a window
	   above the faulting stack pointer) before it is read through; a corrupt
	   chain simply truncates the trace instead of chasing garbage. */
	const uint32_t walkLo = (uint32_t)p;
	const uint32_t walkHi = walkLo + 0x10000;
#define FRAME_OK(f)  ((f) != 0 && (((uint32_t)(f) & 7u) == 0) && \
                      (uint32_t)(f) >= walkLo && (uint32_t)(f) < walkHi)

	pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[11]);

	for(i = 0; i < CPU_STACK_TRACE_DEPTH && FRAME_OK(p); p = p->up, i++) {
		if(i % 4)
            pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[12]);
		else {
			if(i > 0)
                pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[13]);
			else
                pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[14]);
		}

		switch(i) {
			case 0:
				if(pc)
                    pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[15],pc);
				break;
			case 1:
				if(!l)
                    l = (frame_rec_t)mfspr(8);
				pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[15],(void*)l);
				break;
			default:
				/* p->up->lr reads through two untrusted links: validate the
				   next frame before reading through it (the loop also stops
				   at an invalid link on the next iteration). */
				if(!FRAME_OK(p->up))
					break;
				pos = addf(buf, pos, (int)sizeof(buf), exception_print_formats[15],(void*)(p->up->lr));
				break;
		}
	}
#undef FRAME_OK

	/* The code dump at the faulting PC was removed: reading arbitrary memory
	   can fault inside the exception handler itself. */

    buf[sizeof(buf) - 1] = '\0';
    OSFatal(buf);
    return 1;
}

/* Wrappers match wut's OSExceptionCallbackFn (BOOL(OSContext*)) exactly, so
   registration needs no cast and -Wcast-function-type cannot fire here. */
static BOOL dsi_exception_cb(OSContext * context) {
    return (BOOL)exception_cb(context, 0);
}
static BOOL isi_exception_cb(OSContext * context) {
    return (BOOL)exception_cb(context, 1);
}
static BOOL program_exception_cb(OSContext * context) {
    return (BOOL)exception_cb(context, 2);
}

void setup_os_exceptions(void) {
    OSSetExceptionCallback(OS_EXCEPTION_DSI, dsi_exception_cb);
    OSSetExceptionCallback(OS_EXCEPTION_ISI, isi_exception_cb);
    OSSetExceptionCallback(OS_EXCEPTION_PROGRAM, program_exception_cb);
}
