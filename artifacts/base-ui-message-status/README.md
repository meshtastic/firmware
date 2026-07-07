# Base UI Message Status Evidence

Screenshot evidence for Base UI message-status wording aligned with:

- https://github.com/meshtastic/design/issues/43
- https://github.com/meshtastic/firmware/issues/10904

All screenshots are 320x200 grayscale Base UI captures, with a combined review sheet at
`artifacts/base-ui-message-status/contact-sheet.png`.

| State                                              | Expected text                         | Screenshot                      |
| -------------------------------------------------- | ------------------------------------- | ------------------------------- |
| Sending                                            | `Sending...`                          | `sending.png`                   |
| Channel implicit ACK                               | `Delivered to mesh`                   | `channel-delivered.png`         |
| DM explicit ACK                                    | `Delivered to recipient`              | `dm-delivered.png`              |
| DM relay ACK                                       | `Relayed, not confirmed by recipient` | `dm-relayed.png`                |
| No ACK / max retransmit / generic delivery failure | `Failed to deliver to mesh`           | `failed-mesh.png`               |
| Message too large                                  | `Message is too large to send`        | `too-large.png`                 |
| No channel / decode mismatch                       | `Channel/key mismatch`                | `no-channel.png`                |
| No radio interface                                 | `No radio interface`                  | `no-interface.png`              |
| Duty cycle limit                                   | `Duty cycle limit`                    | `duty-cycle-limit.png`          |
| Rate limit exceeded                                | `Rate limited`                        | `rate-limited.png`              |
| No app response                                    | `No app response`                     | `no-app-response.png`           |
| Bad request                                        | `Invalid request`                     | `invalid-request.png`           |
| Not authorized                                     | `Not authorized`                      | `not-authorized.png`            |
| Admin bad session key                              | `Admin session expired`               | `admin-session-expired.png`     |
| Admin public key unauthorized                      | `Admin key not authorized`            | `admin-key-not-authorized.png`  |
| PKI failed                                         | `Could not send encrypted message`    | `pki-failed.png`                |
| PKI send missing recipient key                     | `Recipient key unavailable`           | `recipient-key-unavailable.png` |
| PKI recipient missing sender key                   | `Recipient needs your key`            | `recipient-needs-your-key.png`  |

Banner evidence uses the same shared status text helper:

| State                            | Screenshot                             |
| -------------------------------- | -------------------------------------- |
| Delivered to mesh banner         | `banner-delivered-mesh.png`            |
| Delivered to recipient banner    | `banner-delivered-recipient.png`       |
| Relayed DM banner                | `banner-relayed.png`                   |
| Failed delivery banner           | `banner-failed-mesh.png`               |
| Recipient key unavailable banner | `banner-recipient-key-unavailable.png` |
| Recipient needs your key banner  | `banner-recipient-needs-your-key.png`  |
