import type { SystemInfo } from '@/hooks/useAC2Socket';
import styles from '@/app/page.module.css';

interface Props {
  sysInfo: SystemInfo;
  latestSeq: number;
}

export default function Footer({ sysInfo, latestSeq }: Props) {
  return (
    <footer className={styles.footer}>
      <span>STM32F407G-DISC1 · AC2 v2 · 115200 8N1</span>
      {sysInfo.git_sha && <span>git:{sysInfo.git_sha}</span>}
      <span style={{ marginLeft: 'auto', color: 'var(--text-faint)' }}>
        seq {latestSeq}
      </span>
    </footer>
  );
}
