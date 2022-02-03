#ifndef __PERFETTO_TRACE_INCLUDE__
#define __PERFETTO_TRACE_INCLUDE__

#ifdef _USE_PERFETTO_TRACE_
#include <inttypes.h>
#include <perfetto.h>

PERFETTO_DEFINE_CATEGORIES(
    perfetto::Category("rendering")
        .SetDescription("Events from the graphics subsystem"),
    perfetto::Category("emulator")
        .SetDescription("Events for the saturn emulation"));

#define TRACE_RENDER( A )  TRACE_EVENT("rendering", A)
#define TRACE_RENDER_SUB_BEGIN( A ) TRACE_EVENT_BEGIN("rendering", A)
#define TRACE_RENDER_SUB_END( ) TRACE_EVENT_END("rendering")

#define TRACE_EMULATOR( A )  TRACE_EVENT("emulator", A)
#define TRACE_EMULATOR_SUB_BEGIN( A ) TRACE_EVENT_BEGIN("emulator", A)
#define TRACE_EMULATOR_SUB_END( ) TRACE_EVENT_END("emulator")

#else
#define TRACE_RENDER( A )
#define TRACE_RENDER_SUB_BEGIN( A )
#define TRACE_RENDER_SUB_END( )

#define TRACE_EMULATOR( A )
#define TRACE_EMULATOR_SUB_BEGIN( A )
#define TRACE_EMULATOR_SUB_END( )
#endif

#endif