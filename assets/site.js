document.querySelectorAll('a[href="#installer-help"]').forEach((link) => {
  link.addEventListener('click', () => {
    document.getElementById('installer-help').open = true;
  });
});

if (window.location.hash === '#installer-help') {
  document.getElementById('installer-help').open = true;
}

document.getElementById('copy-path').addEventListener('click', async () => {
  const path = document.getElementById('plugin-path').textContent;
  const status = document.getElementById('copy-status');
  try {
    await navigator.clipboard.writeText(path);
    status.textContent = 'フォルダーのパスをコピーしました。';
  } catch {
    status.textContent = '上のパスを選択してコピーしてください。';
  }
});
