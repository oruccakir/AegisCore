'use client';
import { useEffect, useRef } from 'react';
import styles from './RadarDisplay.module.css';

interface Props { state: number; }

const TRAIL_COUNT = 60;

export default function RadarDisplay({ state }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const angleRef  = useRef(0);
  const trailsRef = useRef<number[]>([]);
  const rafRef    = useRef<number>(0);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d')!;

    const SIZE = 300;
    canvas.width  = SIZE;
    canvas.height = SIZE;
    const cx = SIZE / 2;
    const cy = SIZE / 2;
    const R  = SIZE / 2 - 8;

    const isActive = state === 1 || state === 2;
    const speed    = state === 2 ? 0.04 : 0.025;

    function draw() {
      ctx.clearRect(0, 0, SIZE, SIZE);

      // Background
      ctx.fillStyle = '#020a03';
      ctx.beginPath();
      ctx.arc(cx, cy, R + 4, 0, Math.PI * 2);
      ctx.fill();

      // Range rings
      for (let i = 1; i <= 4; i++) {
        ctx.beginPath();
        ctx.arc(cx, cy, (R * i) / 4, 0, Math.PI * 2);
        ctx.strokeStyle = i === 4 ? '#0a3015' : '#061a0b';
        ctx.lineWidth = i === 4 ? 1.5 : 0.8;
        ctx.stroke();
      }

      // Crosshairs
      ctx.strokeStyle = '#061a0b';
      ctx.lineWidth = 0.8;
      ctx.beginPath(); ctx.moveTo(cx - R, cy); ctx.lineTo(cx + R, cy); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(cx, cy - R); ctx.lineTo(cx, cy + R); ctx.stroke();

      if (!isActive) {
        // Idle — dim static display
        ctx.fillStyle = '#0a2e10';
        ctx.font = '10px Share Tech Mono';
        ctx.textAlign = 'center';
        ctx.fillText('STANDBY', cx, cy + 4);
        return;
      }

      // Sweep trail
      const trailLen = Math.PI * 1.1;
      for (let i = 0; i < TRAIL_COUNT; i++) {
        const frac    = i / TRAIL_COUNT;
        const trailA  = angleRef.current - frac * trailLen;
        const alpha   = (1 - frac) * 0.55;
          ctx.beginPath();
        ctx.moveTo(cx, cy);
        ctx.arc(cx, cy, R, trailA, trailA + trailLen / TRAIL_COUNT);
        ctx.closePath();
        ctx.fillStyle = `rgba(0, 255, 65, ${alpha * 0.12})`;
        ctx.fill();
      }

      // Sweep line
      const sweep = angleRef.current;
      const grd = ctx.createLinearGradient(cx, cy,
        cx + Math.cos(sweep) * R,
        cy + Math.sin(sweep) * R);
      grd.addColorStop(0, 'rgba(0,255,65,0)');
      grd.addColorStop(1, 'rgba(0,255,65,0.9)');
      ctx.beginPath();
      ctx.moveTo(cx, cy);
      ctx.lineTo(cx + Math.cos(sweep) * R, cy + Math.sin(sweep) * R);
      ctx.strokeStyle = grd;
      ctx.lineWidth = 2;
      ctx.stroke();

      // Blips (simulated in TRACK mode)
      if (state === 2) {
        for (const a of trailsRef.current) {
          const diff = ((angleRef.current - a) % (Math.PI * 2) + Math.PI * 2) % (Math.PI * 2);
          if (diff < Math.PI * 0.3) {
            const age   = diff / (Math.PI * 0.3);
            const dist  = R * 0.55;
            const bx    = cx + Math.cos(a) * dist;
            const by    = cy + Math.sin(a) * dist;
            ctx.beginPath();
            ctx.arc(bx, by, 3, 0, Math.PI * 2);
            ctx.fillStyle = `rgba(0,255,65,${1 - age})`;
            ctx.fill();
            ctx.beginPath();
            ctx.arc(bx, by, 6, 0, Math.PI * 2);
            ctx.fillStyle = `rgba(0,255,65,${(1 - age) * 0.3})`;
            ctx.fill();
          }
        }
      }

      // Center dot
      ctx.beginPath();
      ctx.arc(cx, cy, 3, 0, Math.PI * 2);
      ctx.fillStyle = '#00ff41';
      ctx.fill();

      angleRef.current += speed;
      if (angleRef.current > Math.PI * 2) {
        angleRef.current -= Math.PI * 2;
        if (state === 2 && trailsRef.current.length < 5) {
          trailsRef.current.push(Math.random() * Math.PI * 2);
        }
      }
    }

    function loop() {
      draw();
      rafRef.current = requestAnimationFrame(loop);
    }

    loop();
    return () => cancelAnimationFrame(rafRef.current);
  }, [state]);

  return (
    <div className={styles.wrap}>
      <canvas ref={canvasRef} className={styles.canvas} />
      <div className={styles.ring} />
    </div>
  );
}
