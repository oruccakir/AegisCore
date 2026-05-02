'use client';

import { useEffect, useRef, useState } from 'react';
import type { DetectionInfo, OutCmd } from '@/hooks/useAC2Socket';
import styles from '@/app/page.module.css';

interface Props {
  detection: DetectionInfo | null;
  connected: boolean;
  send: (cmd: OutCmd) => void;
}

export default function VisionPanel({ detection, connected, send }: Props) {
  const videoRef = useRef<HTMLVideoElement | null>(null);
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const streamRef = useRef<MediaStream | null>(null);
  const [active, setActive] = useState(false);
  const [cameraError, setCameraError] = useState<string | null>(null);

  useEffect(() => {
    return () => {
      streamRef.current?.getTracks().forEach((track) => track.stop());
    };
  }, []);

  useEffect(() => {
    if (!active || !connected) return;

    const interval = window.setInterval(() => {
      const video = videoRef.current;
      const canvas = canvasRef.current;
      if (!video || !canvas || video.readyState < HTMLMediaElement.HAVE_CURRENT_DATA) return;

      const ctx = canvas.getContext('2d');
      if (!ctx) return;

      canvas.width = 320;
      canvas.height = 240;
      ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
      const jpegB64 = canvas.toDataURL('image/jpeg', 0.55).split(',')[1];
      if (jpegB64) send({ type: 'cmd.vision_frame', jpeg_b64: jpegB64 });
    }, 500);

    return () => window.clearInterval(interval);
  }, [active, connected, send]);

  async function startCamera() {
    setCameraError(null);
    try {
      const stream = await navigator.mediaDevices.getUserMedia({
        video: { width: 640, height: 480, facingMode: 'environment' },
        audio: false,
      });
      streamRef.current = stream;
      if (videoRef.current) {
        videoRef.current.srcObject = stream;
        await videoRef.current.play();
      }
      setActive(true);
    } catch (err) {
      setCameraError(err instanceof Error ? err.message : 'camera unavailable');
      setActive(false);
    }
  }

  function stopCamera() {
    streamRef.current?.getTracks().forEach((track) => track.stop());
    streamRef.current = null;
    if (videoRef.current) videoRef.current.srcObject = null;
    setActive(false);
  }

  const detected = detection?.class_id === 1;
  const label = detection ? detection.class_name.toUpperCase() : 'NO SAMPLE';
  const confidence = detection ? `${detection.confidence}%` : '--';

  return (
    <div className={styles.panel}>
      <div className={styles.panelTitle}>VISION</div>
      <div className={styles.visionGrid}>
        <div className={styles.videoShell}>
          <video ref={videoRef} className={styles.video} muted playsInline />
          <canvas ref={canvasRef} className={styles.hiddenCanvas} />
          {!active && <div className={styles.videoPlaceholder}>CAMERA OFF</div>}
        </div>
        <div className={styles.visionReadout}>
          <div className={styles.detectionLabel} data-detected={detected}>
            {label}
          </div>
          <div className={styles.detectionMeta}>
            CONF {confidence}
            {detection && <span> · {detection.latency_ms}ms</span>}
          </div>
          {cameraError && <div className={styles.cameraError}>{cameraError}</div>}
          <div className={styles.btnRow}>
            <button
              className={`${styles.btn} ${styles.btnSecondary}`}
              disabled={!connected || active}
              onClick={startCamera}
            >
              START
            </button>
            <button className={`${styles.btn} ${styles.btnSecondary}`} disabled={!active} onClick={stopCamera}>
              STOP
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
