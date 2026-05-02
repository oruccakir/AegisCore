import styles from '@/app/page.module.css';

interface Props {
  isFailSafe: boolean;
  status: string;
}

export default function AlertBanners({ isFailSafe, status }: Props) {
  return (
    <>
      {isFailSafe && (
        <div className={styles.failBanner}>
          ⚠ FAIL-SAFE ENGAGED — SYSTEM LOCKED
        </div>
      )}
      {status === 'disconnected' && (
        <div className={styles.warnBanner}>
          ● GATEWAY DISCONNECTED — RECONNECTING…
        </div>
      )}
    </>
  );
}
