#ifndef MWLUA_NETBINDINGS_H
#define MWLUA_NETBINDINGS_H

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    // Package `openmw.net`: a small host/client message transport for global and menu scripts.
    // Every context gets the same package, there is one network session per Lua state.
    sol::table initNetPackage(const Context& context);
}

#endif // MWLUA_NETBINDINGS_H
