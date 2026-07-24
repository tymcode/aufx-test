#import <Foundation/Foundation.h>
#include "PluginScannerOOP.h"

void aufxSetScannerWorkerProcessName()
{
    @autoreleasepool
    {
        [[NSProcessInfo processInfo] setProcessName:@"AU Effects Explorer (plugin scan)"];
    }
}
