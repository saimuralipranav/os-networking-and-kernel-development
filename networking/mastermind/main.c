#include "protocol.h"
#include "logger.h"
#include "discovery.h"
#include "tcp_transport.h"
#include "game.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef enum {
    STATE_LOBBY = 0,
    STATE_CHALLENGE_PENDING_OUT,
    STATE_CHALLENGE_PENDING_IN,
    STATE_GAMEPLAY,
    STATE_GAME_OVER,
    STATE_DISCONNECTED
} AppState;

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char *argv[]) {
    int logging = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--log") == 0) {
            logging = 1;
        }
    }

    logger_init(logging);
    logger_log("Mastermind launched. logging=%d", logging);

    /* Prompt for player name */
    char local_name[MM_MAX_NAME_LEN];
    printf("Enter your name: ");
    fflush(stdout);
    if (!fgets(local_name, sizeof(local_name), stdin)) {
        return 0;
    }
    local_name[strcspn(local_name, "\r\n")] = '\0';
    if (strlen(local_name) == 0) {
        strcpy(local_name, "Player");
    }

    /* Initialize TCP Listener */
    TcpManager tcp;
    if (tcp_init_listener(&tcp, 0) < 0) {
        fprintf(stderr, "Failed to bind TCP listener\n");
        return 1;
    }

    /* Initialize UDP Discovery */
    DiscoveryManager dm;
    if (discovery_init(&dm, local_name, tcp.listen_port, 0) < 0) {
        fprintf(stderr, "Failed to initialize UDP discovery\n");
        return 1;
    }

    AppState state = STATE_LOBBY;
    GameState game;
    char peer_name[MM_MAX_NAME_LEN] = "Peer";
    char peer_ip[64] = "";
    uint16_t peer_port = 0;

    char stdin_buf[256];
    size_t stdin_len = 0;
    int stdin_closed = 0;
    set_nonblocking(STDIN_FILENO);

    int lobby_needs_render = 1;
    char pending_mastermind_guess[MM_SEQ_LEN + 1] = "";
    int awaiting_feena_feedback_approval = 0;

    while (1) {
        /* Run Discovery Ticks */
        if (state == STATE_LOBBY) {
            discovery_broadcast_tick(&dm);
            if (discovery_recv_tick(&dm)) {
                lobby_needs_render = 1;
            }
            if (discovery_prune_expired(&dm)) {
                lobby_needs_render = 1;
            }
            if (lobby_needs_render) {
                ui_render_lobby(dm.players, dm.player_count, local_name, tcp.listen_port, 0);
                lobby_needs_render = 0;
            }
        }

        /* Setup poll */
        struct pollfd pfd[4];
        int nfds = 0;

        int stdin_pfd_idx = -1;
        if (!stdin_closed) {
            pfd[nfds].fd = STDIN_FILENO;
            pfd[nfds].events = POLLIN;
            pfd[nfds].revents = 0;
            stdin_pfd_idx = nfds++;
        }

        pfd[nfds].fd = dm.udp_fd;
        pfd[nfds].events = POLLIN;
        pfd[nfds].revents = 0;
        int disc_pfd_idx = nfds++;

        int net_pfd_idx = -1;
        int tcp_listen_idx = -1;

        if (state == STATE_LOBBY) {
            pfd[nfds].fd = tcp.listen_fd;
            pfd[nfds].events = POLLIN;
            pfd[nfds].revents = 0;
            tcp_listen_idx = nfds++;
        } else if (tcp.conn_fd >= 0) {
            pfd[nfds].fd = tcp.conn_fd;
            pfd[nfds].events = POLLIN;
            pfd[nfds].revents = 0;
            net_pfd_idx = nfds++;
        }

        int pr = poll(pfd, nfds, 200);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* Check Discovery socket */
        if (disc_pfd_idx >= 0 && (pfd[disc_pfd_idx].revents & POLLIN)) {
            if (discovery_recv_tick(&dm)) {
                if (state == STATE_LOBBY) lobby_needs_render = 1;
            }
        }

        /* Check TCP Listener */
        if (tcp_listen_idx >= 0 && (pfd[tcp_listen_idx].revents & POLLIN)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int new_sock = accept(tcp.listen_fd, (struct sockaddr *)&client_addr, &client_len);
            if (new_sock >= 0) {
                set_nonblocking(new_sock);
                tcp.conn_fd = new_sock;
                inet_ntop(AF_INET, &client_addr.sin_addr, peer_ip, sizeof(peer_ip));
                peer_port = ntohs(client_addr.sin_port);
                state = STATE_CHALLENGE_PENDING_IN;
            }
        }

        /* Check Connected Socket */
        GameMessage msg;
        memset(&msg, 0, sizeof(msg));
        int has_msg = 0;

        if (net_pfd_idx >= 0 && (pfd[net_pfd_idx].revents & POLLIN)) {
            int r = tcp_recv_msg(tcp.conn_fd, tcp.read_buf, &tcp.read_len, &msg);
            if (r > 0) {
                has_msg = 1;
            } else if (r < 0) {
                /* Detect TCP Disconnection */
                if (state == STATE_GAMEPLAY || state == STATE_CHALLENGE_PENDING_OUT || state == STATE_CHALLENGE_PENDING_IN) {
                    state = STATE_DISCONNECTED;
                    printf("\n%s disconnected. Press enter to go home.\n", peer_name);
                    fflush(stdout);
                }
            }
        }

        /* Handle Messages */
        if (has_msg) {
            if (msg.type == MSG_CHALLENGE) {
                strncpy(peer_name, msg.text, sizeof(peer_name) - 1);
                state = STATE_CHALLENGE_PENDING_IN;
                printf("\nChallenge from %s! Accept? (yes/no): ", peer_name);
                fflush(stdout);
            } else if (msg.type == MSG_ACCEPT) {
                game_init(&game, 1);
                state = STATE_GAMEPLAY;
                printf("\n%s accepted the challenge! You are the Mastermind (Feena).\n", peer_name);
                printf("Enter 5-digit master sequence: ");
                fflush(stdout);
            } else if (msg.type == MSG_REJECT) {
                printf("\n%s rejected the challenge. Returning to lobby...\n", peer_name);
                if (tcp.conn_fd >= 0) {
                    close(tcp.conn_fd);
                    tcp.conn_fd = -1;
                }
                state = STATE_LOBBY;
                lobby_needs_render = 1;
            } else if (msg.type == MSG_ATTEMPT) {
                if (game.is_mastermind) {
                    strncpy(pending_mastermind_guess, msg.text, MM_SEQ_LEN);
                    pending_mastermind_guess[MM_SEQ_LEN] = '\0';
                    char computed_fb[MM_SEQ_LEN + 1];
                    game_compute_feedback(game.master_seq, pending_mastermind_guess, computed_fb);

                    printf("\nReceived attempt: %s | Feedback calculated: %s\nPress Enter to send feedback to %s: ",
                           pending_mastermind_guess, computed_fb, peer_name);
                    fflush(stdout);
                    awaiting_feena_feedback_approval = 1;
                }
            } else if (msg.type == MSG_FEEDBACK) {
                if (!game.is_mastermind) {
                    int finished = game_record_turn(&game, game.attempts[game.current_round], msg.text);
                    ui_render_board(&game);
                    if (finished) {
                        state = STATE_GAME_OVER;
                        if (game.codebreaker_won) {
                            printf("\nYou cracked the sequence in %d attempts! WINNER!\n", game.current_round);
                        } else {
                            printf("\n12 attempts exhausted! Game over.\n");
                        }
                        printf("Press enter to go home.\n");
                        fflush(stdout);
                    } else {
                        printf("Attempt %d/12: ", game.current_round + 1);
                        fflush(stdout);
                    }
                }
            } else if (msg.type == MSG_END) {
                strncpy(game.master_seq, msg.text, MM_SEQ_LEN);
                game.master_seq[MM_SEQ_LEN] = '\0';
                game.game_over = 1;
                ui_render_board(&game);
                state = STATE_GAME_OVER;
                printf("\nMaster sequence was: %s\nPress enter to go home.\n", game.master_seq);
                fflush(stdout);
            } else if (msg.type == MSG_DISCONNECT) {
                state = STATE_DISCONNECTED;
                printf("\n%s disconnected. Press enter to go home.\n", peer_name);
                fflush(stdout);
            }
        }

        /* Check Stdin */
        if (stdin_pfd_idx >= 0 && (pfd[stdin_pfd_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
            char ch;
            while (1) {
                ssize_t n = read(STDIN_FILENO, &ch, 1);
                if (n <= 0) {
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                        break;
                    }
                    stdin_closed = 1;
                    break;
                }
                if (ch == '\r' || ch == '\n') {
                    stdin_buf[stdin_len] = '\0';
                    char input[256];
                    strncpy(input, stdin_buf, sizeof(input) - 1);
                    input[sizeof(input) - 1] = '\0';
                    stdin_len = 0;

                    if (state == STATE_LOBBY) {
                        if (strncmp(input, "challenge ", 10) == 0) {
                            int target_id = atoi(input + 10);
                            const DiscoveredPlayer *target = discovery_find_by_id(&dm, target_id);
                            if (!target) {
                                printf("Invalid player ID %d. > ", target_id);
                                fflush(stdout);
                            } else {
                                strncpy(peer_name, target->name, sizeof(peer_name) - 1);
                                strncpy(peer_ip, target->ip, sizeof(peer_ip) - 1);
                                peer_port = target->port;

                                printf("Sending challenge to %s (%s:%u)...\n", peer_name, peer_ip, peer_port);
                                fflush(stdout);

                                int fd = tcp_connect_peer(peer_ip, peer_port);
                                if (fd < 0) {
                                    printf("Failed to connect to %s. Returning to lobby.\n> ", peer_name);
                                    fflush(stdout);
                                } else {
                                    tcp.conn_fd = fd;
                                    tcp_send_msg(tcp.conn_fd, MSG_CHALLENGE, local_name);
                                    state = STATE_CHALLENGE_PENDING_OUT;
                                }
                            }
                        }
                    } else if (state == STATE_CHALLENGE_PENDING_IN) {
                        if (strcmp(input, "yes") == 0) {
                            game_init(&game, 0);
                            state = STATE_GAMEPLAY;
                            tcp_send_msg(tcp.conn_fd, MSG_ACCEPT, local_name);
                            printf("\nYou accepted! Waiting for %s to set the master sequence...\n", peer_name);
                            fflush(stdout);
                        } else if (strcmp(input, "no") == 0) {
                            tcp_send_msg(tcp.conn_fd, MSG_REJECT, local_name);
                            close(tcp.conn_fd);
                            tcp.conn_fd = -1;
                            state = STATE_LOBBY;
                            lobby_needs_render = 1;
                        } else {
                            printf("Please answer 'yes' or 'no': ");
                            fflush(stdout);
                        }
                    } else if (state == STATE_GAMEPLAY) {
                        if (game.is_mastermind) {
                            if (strcmp(game.master_seq, "*****") == 0) {
                                if (!game_validate_sequence(input)) {
                                    printf("Invalid sequence! Must be exactly 5 digits (0-9).\nEnter 5-digit master sequence: ");
                                    fflush(stdout);
                                } else {
                                    strncpy(game.master_seq, input, MM_SEQ_LEN);
                                    game.master_seq[MM_SEQ_LEN] = '\0';
                                    ui_render_board(&game);
                                    printf("\nMaster sequence set! Waiting for %s to make attempt 1/12...\n", peer_name);
                                    fflush(stdout);
                                }
                            } else if (awaiting_feena_feedback_approval) {
                                char feedback[MM_SEQ_LEN + 1];
                                game_compute_feedback(game.master_seq, pending_mastermind_guess, feedback);
                                int finished = game_record_turn(&game, pending_mastermind_guess, feedback);
                                ui_render_board(&game);
                                tcp_send_msg(tcp.conn_fd, MSG_FEEDBACK, feedback);
                                awaiting_feena_feedback_approval = 0;

                                if (finished) {
                                    state = STATE_GAME_OVER;
                                    tcp_send_msg(tcp.conn_fd, MSG_END, game.master_seq);

                                    if (game.codebreaker_won) {
                                        printf("\n%s cracked your code in %d attempts!\n", peer_name, game.current_round);
                                    } else {
                                        printf("\nAttempts exhausted! %s failed to crack the code.\n", peer_name);
                                    }
                                    printf("Press enter to go home.\n");
                                    fflush(stdout);
                                } else {
                                    printf("\nWaiting for %s's next attempt...\n", peer_name);
                                    fflush(stdout);
                                }
                            }
                        } else {
                            if (!game_validate_sequence(input)) {
                                printf("Invalid guess! Must be exactly 5 digits (0-9).\nAttempt %d/12: ",
                                       game.current_round + 1);
                                fflush(stdout);
                            } else {
                                strncpy(game.attempts[game.current_round], input, MM_SEQ_LEN);
                                game.attempts[game.current_round][MM_SEQ_LEN] = '\0';
                                tcp_send_msg(tcp.conn_fd, MSG_ATTEMPT, input);
                                printf("Sent attempt %s. Waiting for %s's feedback...\n", input, peer_name);
                                fflush(stdout);
                            }
                        }
                    } else if (state == STATE_GAME_OVER || state == STATE_DISCONNECTED) {
                        if (tcp.conn_fd >= 0) {
                            close(tcp.conn_fd);
                            tcp.conn_fd = -1;
                        }
                        state = STATE_LOBBY;
                        lobby_needs_render = 1;
                    }
                    break;
                } else if (stdin_len < sizeof(stdin_buf) - 1) {
                    stdin_buf[stdin_len++] = ch;
                }
            }
        }
    }

    discovery_close(&dm);
    tcp_close(&tcp);
    logger_close();
    return 0;
}
