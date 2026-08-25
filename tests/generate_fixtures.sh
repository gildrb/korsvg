#!/bin/sh
set -eu

output=${1:-"$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)/fixtures"}
mkdir -p "$output"

cat > "$output/geometry.svg" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <rect width="16" height="16" fill="#ff0000"/>
  <circle cx="24" cy="8" r="7" fill="#00ff00"/>
  <ellipse cx="8" cy="24" rx="7" ry="5" fill="#0000ff"/>
  <path d="M17 31 L24 17 L31 31 Z" fill="#ffff00"/>
</svg>
EOF

cat > "$output/transforms.svg" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <rect width="8" height="8" fill="#ff0000" transform="translate(4 4)"/>
  <rect width="6" height="6" fill="#00ff00" transform="translate(18 2) scale(2)"/>
  <rect x="2" y="20" width="8" height="4" fill="#0000ff" transform="rotate(45 6 22)"/>
</svg>
EOF

cat > "$output/styles.svg" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32">
  <rect x="2" y="2" width="12" height="12" fill="rgb(255,0,255)" stroke="#00ffff" stroke-width="2"/>
  <path d="M18 2 H30 V14 H18 Z M21 5 V11 H27 V5 Z" fill="#ffa500" fill-rule="evenodd"/>
  <polyline points="3,27 8,18 13,27" fill="none" stroke="#ffffff" stroke-width="3" stroke-linejoin="round"/>
</svg>
EOF

cat > "$output/aspect.svg" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 10">
  <rect width="20" height="10" fill="#800080"/>
  <rect x="0" y="0" width="2" height="10" fill="#00ff00"/>
</svg>
EOF

cat > "$output/alpha.svg" <<'EOF'
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">
  <rect width="16" height="16" fill="#ff0000" fill-opacity="0.5"/>
  <rect x="8" width="8" height="16" fill="#0000ff" opacity="0.5"/>
</svg>
EOF
