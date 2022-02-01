#ifndef __PERFETTO_TRACE_INCLUDE__
#define __PERFETTO_TRACE_INCLUDE__

#ifdef _USE_PERFETTO_TRACE_
#include <perfetto.h>

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("rendering")
        .SetDescription("Events from the graphics subsystem"),
    perfetto::Category("emulator")
        .SetDescription("Events for the saturn emulation"));

#define LEVEL_TRACE 1

#define TRACE_RENDER( A )  TRACE_EVENT("rendering", A)
#define TRACE_RENDER_SUB_BEGIN( A ) TRACE_EVENT_BEGIN("rendering", A)
#define TRACE_RENDER_SUB_END() TRACE_EVENT_END("rendering")

#define TRACE_EMULATOR( A )  TRACE_EVENT("emulator", A)
#define TRACE_EMULATOR_SUB_BEGIN( A, I ) if (I <= LEVEL_TRACE) TRACE_EVENT_BEGIN("emulator", A)
#define TRACE_EMULATOR_SUB_END( I ) if (I <= LEVEL_TRACE) TRACE_EVENT_END("emulator")
#define TRACE_EMULATOR_COND( A, B, C )  TRACE_EVENT("emulator", (A)?B:C)

#else
#define TRACE_RENDER( A )
#define TRACE_RENDER_SUB_BEGIN( A )
#define TRACE_RENDER_SUB_END( A )

#define TRACE_EMULATOR( A )
#define TRACE_EMULATOR_SUB_BEGIN( A, I )
#define TRACE_EMULATOR_SUB_END( I )
#define TRACE_EMULATOR_COND( A, B, C )

#endif

#endif