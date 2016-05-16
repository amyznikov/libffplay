package com.sis.ffplay;

public final class log
{
  public static final void d(final String tag, final String format, Object... args) {
    android.util.Log.d(tag, String.format(format, args));
  }

  public static final void v(final String tag, final String format, Object... args) {
    android.util.Log.v(tag, String.format(format, args));
  }
  
  public static final void i(final String tag, final String format, Object... args) {
    android.util.Log.i(tag, String.format(format, args));
  }
 
  public static final void w(final String tag, final String format, Object... args) {
    android.util.Log.w(tag, String.format(format, args));
  }
  
  public static final void e(final String tag, final String format, Object... args) {
    android.util.Log.e(tag, String.format(format, args));
  }

  public static final void wtf(final String tag, final String format, Object... args) {
    android.util.Log.wtf(tag, String.format(format, args));
  }
  
}
