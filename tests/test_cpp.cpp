#include "../korsvg.h"

int main()
{
    KorSVGTypeID type = KorSVGDocumentGetTypeID();
    return type == 0;
}
