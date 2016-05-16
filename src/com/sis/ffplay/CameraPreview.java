package com.sis.ffplay;


import java.lang.ref.WeakReference;
import java.util.List;

import com.sis.ffplay.log;
import android.content.Context;
import android.view.Display;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;
import android.hardware.Camera;
import android.os.Handler;
import android.os.Message;
import android.graphics.Color;
import android.graphics.ImageFormat;
import android.util.AttributeSet;


/**
 * @author amyznikov
 * @see http://developer.android.com/guide/topics/media/camera.html
 */
@SuppressWarnings("deprecation")
public class CameraPreview extends SurfaceView
  implements SurfaceHolder.Callback, Camera.PreviewCallback 
{

  static {
    System.loadLibrary("ffplay");
  }
  
  private static final String TAG = "ffplay/CameraPreview";
  
  public static final int STATE_IDLE = 0;
  public static final int STATE_PREVIEW = 1;
  public static final int STATE_STREAMING = 2;

  public static final int STREAM_STATE_IDLE = 0;
  public static final int STREAM_STATE_starting = 1;
  public static final int STREAM_STATE_CONNECTING = 2;
  public static final int STREAM_STATE_ESTABLISHED = 3;
  public static final int STREAM_STATE_DISCONNECTING = 4;
  
  
  public static final int KERR_NONE = 0;
  public static final int KERR_IN_USE = 1;
  public static final int KERR_NOT_READY = 2;
  public static final int KERR_FRAME_SIZE_NOT_SUPPORTED = 3;
  public static final int KERR_CAMERA_OPEN_FAILS = 4;
  public static final int KERR_CAMERA_START_PREVIEW_FAILS = 5;
  public static final int KERR_START_STREAM_FAILS = 6;
  
  
  private Camera camera;
  private Camera.Parameters parameters;
  private int r, w, h, scx, scy;
  boolean have_surface;
  private int state_;
  private EventListener eventListener;
  private AsyncEventHandler h_; 
  private long nativeStream_;

  
  //////////////////////////////////////////////////////////////////////

  public static interface EventListener {
    public void onPreviewStarted();
    public void onPreviewFinished();
    public void onStreamStarted();
    public void onStreamFinished();
    public void onStreamStateChaged(int state, int reason);
  }

    
  public CameraPreview(Context context) {
    super(context);
    construct();
  }

  public CameraPreview(Context context, AttributeSet attrs) {
    super(context, attrs);
    construct();
  }

  public CameraPreview(Context context, AttributeSet attrs, int defStyleAttr) {
    super(context, attrs, defStyleAttr);
    construct();
  }

  
  public void setEventListener(EventListener l) {
    this.eventListener = l;
  }
  
  public final int state() {
    return state_;
  }

  
  public int startPreview(int cameraId) {
    return startPreview(cameraId, 0, 0);
  }
  
  public int startPreview(int cameraId, int cx, int cy) {
    
    log.d(TAG, "ENTER startPreview()");
    
    int status = KERR_NONE;
    
    if ( state_ != STATE_IDLE ) {
      return KERR_IN_USE; 
    }
    
    
    if ( (status = openCamera(cameraId)) != KERR_NONE ) {
      return status;
    }
      
    
    /* Setup video frame geometry 
     * */
    parameters = camera.getParameters();
    
    List<Camera.Size> supportedSizes = parameters.getSupportedPreviewSizes();
    
    //List<Camera.Size> supportedSizes = parameters.getSupportedPictureSizes();
    
    if ( cx > 0 && cy > 0 ) {
      if ( !isFrameSizeSupported(cx, cy, supportedSizes) ) {
        status = KERR_FRAME_SIZE_NOT_SUPPORTED;
      }
    }
    else {
      /* Auto select picture size */
//      Camera.Size size = parameters.getPreferredPreviewSizeForVideo();
//      cx = size.width;
//      cy = size.height;
      cx = 640;
      cy = 480;
      scx = cx;
      scy = cy;
    }

    if (status == KERR_NONE) {

      //parameters.setPictureSize(cx, cy);
      parameters.setPreviewSize(cx, cy);

      if ( !have_surface || (status = startCameraPreview()) == KERR_NONE ) {
        state_ = STATE_PREVIEW;
        emitPreviewStarted();
      }
    }

    if ( status != KERR_NONE ) {
      closeCamera();
    }
    
    log.d(TAG, "LEAVE startPreview()");
    return status;    
  }
  
  
  public void stopPreview() {
    
    switch (state_) {
    case STATE_STREAMING:
      log.d(TAG, "stopStreaming()");
      stopStream();
      // no break here
    case STATE_PREVIEW:
      closeCamera();
      emitPreviewFinished();
      state_ = STATE_IDLE;
      break;
    }
  }
  

  public int startStream(String server, String opts) {
    
    int status = KERR_NONE;

    switch (state_) {
    case STATE_IDLE:
      log.d(TAG, "startStream(): No preview is active");
      return KERR_NOT_READY;
    case STATE_STREAMING:
      log.d(TAG, "startStream(): Already streaming");
      return KERR_IN_USE;
    }
    
    log.d(TAG, "startStream()");
    if ((status = startNativeStream(server, opts)) == KERR_NONE) {
      state_ = STATE_STREAMING;
      emitStreamStarted();
    }
    
    return status;
  }  
  
  public void stopStream() {
    if ( state_ == STATE_STREAMING ) {
      log.d(TAG, "stopStream()");
      stopNativeStream();
      state_ = STATE_PREVIEW;
      emitStreamFinished();
    }
  }

  
  //////////////////////////////////////////////////////////////////////
  
  
  
  private static final int AEVT_STOP_STREAM = 1;
  private static final int AEVT_STREAM_STATE_CHANGED = 2;
  
  // CRAZY.
  // See http://stackoverflow.com/questions/11407943/this-handler-class-should-be-static-or-leaks-might-occur-incominghandler
  private static class AsyncEventHandler extends Handler {
    private final WeakReference<CameraPreview> r;
    public AsyncEventHandler(CameraPreview c) {
      r = new WeakReference<CameraPreview>(c);
    }
    public void handleMessage(Message msg) {

      CameraPreview c = r.get();
      
      if (c != null) {
        
        switch( msg.what ) {
        case AEVT_STOP_STREAM:
          c.stopStream();
          break;
          
        case AEVT_STREAM_STATE_CHANGED:
          if ( c.eventListener != null ) {
            c.eventListener.onStreamStateChaged(msg.arg1, msg.arg2);
          }
          break;
        }
      }
    }
  }; 
  
  
  
  private void construct() {
    h_ = new AsyncEventHandler(this); 
    getHolder().addCallback(this);
  }
  
  /** Open camera device */   
  private int openCamera(int cameraId) {
    try {
      camera = Camera.open(cameraId);
    }
    catch (Exception e) {
      return KERR_CAMERA_OPEN_FAILS;
    }
    return KERR_NONE; 
  }

  
  /** Close camera device */
  private void closeCamera() {
    if (camera != null) {
      stopCameraPreview();
      camera.release();
      camera = null;
    }
  }
  
  
  /** Check if requested frame size is supported */
  private boolean isFrameSizeSupported(int cx, int cy, List<Camera.Size> supportedSizes) {
    for (int i = 0; i < supportedSizes.size(); ++i) {
      Camera.Size s = supportedSizes.get(i);
      if (s.width == cx && s.height == cy) {
        return true;
      }
    }
    return false;
  }

 
  /** allocate frame buffers and start camera preview */
  private int startCameraPreview() {

    log.d(TAG, "ENTER startCameraPreview()");
    
    Camera.Size frameSize;
    int bufferSize;

    int status = KERR_NONE;

    if (camera == null) {
      status = KERR_NOT_READY;
    } else {

      try {
        
        //parameters.setPreviewSize(w, h);
        parameters.setPreviewFormat(ImageFormat.NV21);
        camera.setParameters(parameters);
        camera.setDisplayOrientation(r);
        camera.setPreviewCallbackWithBuffer(this);        

        frameSize = parameters.getPreviewSize();
        bufferSize = frameSize.width * frameSize.height * ImageFormat.getBitsPerPixel(parameters.getPreviewFormat()) / 8;

        for (int i = 0; i < 2; i++) {
          camera.addCallbackBuffer(new byte[bufferSize]);
        }

        /* Start preview */
        setBackgroundColor(Color.TRANSPARENT);
        camera.setPreviewDisplay(getHolder());
        camera.startPreview();
        
      } catch (Exception e) {
        status = KERR_CAMERA_START_PREVIEW_FAILS;
      }

    }

    log.d(TAG, "LEAVE startCameraPreview(): status=%d", status);
    return status;
  }

  private void stopCameraPreview() {
    if (camera != null) {
      try {
        camera.stopPreview();
      } catch (Exception e) {
        // ignore this error
      }
    }
  }
 
  private void emitPreviewStarted() {
    if (eventListener != null) {
      eventListener.onPreviewStarted();
    }
  }

  private void emitPreviewFinished() {
    if (eventListener != null) {
      eventListener.onPreviewFinished();
    }
  }

  private void emitStreamStarted() {
    if (eventListener != null) {
      eventListener.onStreamStarted();
    }
  }

  private void emitStreamFinished() {
    if (eventListener != null) {
      eventListener.onStreamFinished();
    }
  }  
  
  /* Next functions should be private, but java is crazy 
   * */
  
  @Override
  public void surfaceCreated(SurfaceHolder holder) {
    log.d(TAG, "surfaceCreated()");
  }

  @Override
  public void surfaceDestroyed(SurfaceHolder holder) {
    log.d(TAG, "surfaceDestroyed()");
    have_surface = false;
  }

  public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {

    log.d(TAG, "surfaceChanged(format=%d, w=%d, h=%d)", format, w, h);
    
    // Stop preview before making changes
    stopCameraPreview();
    
    // Set preview size and make any resize, rotate or reformatting changes here
    // See http://stackoverflow.com/questions/3841122/android-camera-preview-is-sideways
    //adjustPreviewSize(w, h);
    Display display = ((WindowManager) getContext().getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay();

    switch (display.getRotation()) {
    case Surface.ROTATION_0:
      this.w = scy;
      this.h = scx;
      this.r = 90;
      break;
    case Surface.ROTATION_90:
      this.w = scx;
      this.h = scy;
      this.r = 0;
      break;
    case Surface.ROTATION_180:
      this.w = scy;
      this.h = scx;
      this.r = 0;
      break;
    case Surface.ROTATION_270:
      this.w = scx;
      this.h = scy;
      this.r = 180;
      break;
    default:
      break;
    }
    
    have_surface = true;
    
    if ( state_ != STATE_IDLE ) {
      startCameraPreview();
    }
  
  }
    

  
  /** Camera.PreviewCallback() */
  @Override
  public void onPreviewFrame(byte[] frame, Camera camera) {
    
    if (state_ == STATE_STREAMING && !sendNativeVideoFrame(frame)) {
      log.e(TAG, "sendNativeVideoFrame() fails");
      h_.sendMessageDelayed(h_.obtainMessage(AEVT_STOP_STREAM), 0);
    }
    
    if (camera != null) {
      camera.addCallbackBuffer(frame);
    }
  }
  
 
  //////////////////////////////////////////////////////////////////////

  private native long start_stream(int cx, int cy, int pixfmt, String server, String opts);
  private int startNativeStream(String server, String opts) {
    Camera.Size s = parameters.getPreviewSize();
    nativeStream_ = start_stream(s.width, s.height, parameters.getPreviewFormat(), server, opts); 
    return nativeStream_ == 0 ? KERR_START_STREAM_FAILS : KERR_NONE;
  }

  private native void stop_stream(long handle);
  private void stopNativeStream() {
    if (nativeStream_ != 0) {
      stop_stream(nativeStream_);
      nativeStream_ = 0;
    }
  }

  private native boolean send_video_frame(long handle, byte[] buffer);
  private boolean sendNativeVideoFrame(byte[] buffer) {
    return send_video_frame(nativeStream_, buffer);
  }  
  

  private void onStreamStateChaged(int state, int reason) {
    if (eventListener != null) {
      h_.sendMessageDelayed(h_.obtainMessage(AEVT_STREAM_STATE_CHANGED, state, reason), 0);
    }
    if ( state == STREAM_STATE_IDLE ) {
      h_.sendMessageDelayed(h_.obtainMessage(AEVT_STOP_STREAM), 0);
    }
  }

}