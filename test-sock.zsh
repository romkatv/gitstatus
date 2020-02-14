() {
  emulate -L zsh -o no_bgnice -o monitor -o err_return
  zmodload zsh/system zsh/net/socket zsh/datetime zsh/zselect
  zmodload -F zsh/files b:zf_rm

  local -F start=EPOCHREALTIME
  local sock=${TMPDIR:-/tmp}/test.sock.$sysparams[pid].$EPOCHREALTIME.$RANDOM
  zsocket -l $sock
  local -i sock_fd=REPLY
  local -i req_fd

  exec {req_fd}> >(
    local -i pgid=$sysparams[pid]
    zsocket $sock
    local -i conn_fd=REPLY
    {
      trap '' PIPE
      {
        local -a ready
        local req
        unsetopt err_return
        while zselect -a ready 0; do
          local buf=
          while true; do
            while [[ $buf != *$'\x1e' ]]; do
              sysread 'buf[$#buf+1]' || return
            done
            [[ -t 0 ]]
            if sysread -t 0 'buf[$#buf+1]'; then
              continue
            else
              (( $? == 4 )) || return
              break
            fi
          done
          for req in ${(ps:\x1e:)buf}; do
            print -rnu $conn_fd -- "x"$'\x1e'
          done
        done
      } always {
        kill -- -$pgid
      }
    } &!
  )

  local -i pgid=$sysparams[procsubstpid]
  zsocket -a $sock_fd
  local -i conn_fd=REPLY
  exec {sock_fd}>&-
  zf_rm $sock
  local -F2 took='1e6 * (EPOCHREALTIME - start)'
  print -r -- "startup: $took us"

  sleep 1

  start=EPOCHREALTIME
  repeat 1000; do
    print -rnu $req_fd -- "x"$'\x1e'
    local buf=
    while true; do
      while [[ $buf != *$'\x1e' ]]; do
        sysread -i $conn_fd 'buf[$#buf+1]' || return
      done
      [[ -t $conn_fd ]] || true
      if sysread -t 0 -i $conn_fd 'buf[$#buf+1]'; then
        continue
      else
        (( $? == 4 )) || return
        break
      fi
    done
  done
  local -F2 took='1000 * (EPOCHREALTIME - start)'
  print -r -- "latency: $took us"

  start=EPOCHREALTIME
  repeat 1000; do
    print -rnu $req_fd -- "x"$'\x1e'
  done
  local -i received
  while (( received != 1000 )); do
    local buf=
    sysread -i $conn_fd 'buf[$#buf+1]' || return
    while true; do
      while [[ $buf != *$'\x1e' ]]; do
        sysread -i $conn_fd 'buf[$#buf+1]' || return
      done
      [[ -t $conn_fd ]] || true
      if sysread -t 0 -i $conn_fd 'buf[$#buf+1]'; then
        continue
      else
        (( $? == 4 )) || return
        break
      fi
    done
    received+=${#${(ps:\x1e:)buf}}
  done
  local -F2 took='1000 * (EPOCHREALTIME - start)'
  print -r -- "throughput: $took us/req"

  start=EPOCHREALTIME
  exec {conn_fd}>&-
  exec {req_fd}>&-
  local -F2 took='1e6 * (EPOCHREALTIME - start)'
  print -r -- "shutdown: $took us"
}
