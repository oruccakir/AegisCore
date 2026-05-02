export default function ConnectionDot({ status }: { status: string }) {
  const color = status === 'connected' ? '#00ff41' : status === 'connecting' ? '#ffaa00' : '#ff2222';

  return (
    <span
      style={{
        display: 'inline-block',
        width: 8,
        height: 8,
        borderRadius: '50%',
        background: color,
        boxShadow: `0 0 6px ${color}`,
        animation: status === 'connecting' ? 'blink 1s infinite' : undefined,
      }}
    />
  );
}
