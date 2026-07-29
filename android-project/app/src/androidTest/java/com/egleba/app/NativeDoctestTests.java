package com.egleba.app;

import android.os.Build;
import static org.junit.Assume.assumeTrue;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.Parameterized;
import org.junit.runners.Parameterized.Parameters;

import java.util.Arrays;
import java.util.Collection;
import java.util.Objects;
import java.util.stream.Collectors;

import static org.junit.Assert.assertTrue;

/// On-device native test runner for Android instrumentation tests.
///
/// Executes native C/C++ unit tests bundled in the APK via the Android NDK testing framework.
/// Each native test is discovered at runtime and run as a separate JUnit parameterized test.
///
/// @note Follows Android’s official on-device native testing guidance:
///       https://developer.android.com/ndk/guides/test-native-libraries
///       https://developer.android.com/training/testing/unit-testing/instrumented-unit-tests
///
/// @see https://developer.android.com/ndk/guides/test-native-libraries
/// @see https://developer.android.com/training/testing/unit-testing/instrumented-unit-tests
/// @see gradle task :app:connectedDebugAndroidTest
@RunWith(Parameterized.class)
public final class NativeDoctestTests {

    static {
        assumeTrue("Must run on device", !Build.FINGERPRINT.equals("robolectric"));
        System.loadLibrary("tests");
    }

    private static native String[] getTestNames();

    private static native boolean runTest(String name);

    @Parameterized.Parameter
    public String testName;

    @Parameters(name = "{0}")
    public static Collection<Object[]> data() {
        final String[] names = Objects.requireNonNull(getTestNames(),
                "Native test names must not be null — check that libtests is loaded and exports a valid test suite.");
        return Arrays.stream(names)
                .map(name -> new Object[]{name})
                .collect(Collectors.toList());
    }

    @Test
    public void runNative() {
        assertTrue("Native test failed: " + testName, runTest(testName));
    }
}
