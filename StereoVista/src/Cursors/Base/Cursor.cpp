#include "Cursors/Base/Cursor.h"

namespace Cursor {
    BaseCursor::BaseCursor() :
        m_visible(true),
        m_position(0.0f, 0.0f, 0.0f),
        m_positionValid(false),
        m_name("BaseCursor")
    {
    }

    BaseCursor::~BaseCursor() {
        // Base destructor
    }
}