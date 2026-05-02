'use client';
import AlertBanners from '@/components/dashboard/AlertBanners';
import EventLog from '@/components/dashboard/EventLog';
import Footer from '@/components/dashboard/Footer';
import NlpCommandPanel from '@/components/dashboard/NlpCommandPanel';
import RadarDisplay from '@/components/dashboard/RadarDisplay';
import StateControlPanel from '@/components/dashboard/StateControlPanel';
import StateDescriptionPanel from '@/components/dashboard/StateDescriptionPanel';
import TaskMonitor from '@/components/dashboard/TaskMonitor';
import TelemetryPanel from '@/components/dashboard/TelemetryPanel';
import TopBar from '@/components/dashboard/TopBar';
import VisionPanel from '@/components/dashboard/VisionPanel';
import { useAC2Socket } from '@/hooks/useAC2Socket';
import styles from './page.module.css';

export default function Dashboard() {
  const { status, telemetry, sysInfo, log, tasks, detection, rangeScan, send } = useAC2Socket('ws://localhost:8443');

  const state = telemetry?.state ?? 0;
  const isFailSafe = state === 3;
  const isSafe = !isFailSafe && status === 'connected';
  const rangeScanActive = tasks.some((task) => task.name.startsWith('RngS'));

  return (
    <div className={styles.root}>
      <TopBar state={state} status={status} sysInfo={sysInfo} />

      <main className={styles.main}>
        <section className={styles.left}>
          <div className={styles.radarWrap}>
            <RadarDisplay
              state={state}
              rangeScan={rangeScanActive ? rangeScan : null}
              rangeScanActive={rangeScanActive}
            />
          </div>

          <NlpCommandPanel
            connected={status === 'connected'}
            tasks={tasks}
            send={send}
          />
          <StateDescriptionPanel state={state} />
        </section>

        <section className={styles.center}>
          <TelemetryPanel telemetry={telemetry} sysInfo={sysInfo} />

          <VisionPanel
            detection={detection}
            connected={status === 'connected'}
            send={send}
          />

          <StateControlPanel state={state} isSafe={isSafe} send={send} />

          <TaskMonitor tasks={tasks} send={send} />
          <AlertBanners isFailSafe={isFailSafe} status={status} />
        </section>

        <section className={styles.right}>
          <EventLog entries={log} />
        </section>

      </main>

      <Footer sysInfo={sysInfo} latestSeq={log[0]?.id ?? 0} />
    </div>
  );
}
