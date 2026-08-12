#include <iostream>

int RunCameraMatrixTests();
int RunHeadTransformTests();
int RunMappedBufferTableTests();
int RunMatrixMathTests();

int main() {
    std::cout << "AlienIsolationHeadTracking Tests\n";
    std::cout << "================================\n";

    int failures = 0;
    failures += RunMatrixMathTests();
    failures += RunCameraMatrixTests();
    failures += RunHeadTransformTests();
    failures += RunMappedBufferTableTests();

    if (failures == 0) {
        std::cout << "\nAll tests passed!\n";
        return 0;
    }
    std::cout << "\n" << failures << " test(s) FAILED\n";
    return 1;
}
