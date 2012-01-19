/**
 * File:        src/smime-gate.c
 * Description: S/MIME Gate client service function.
 * Author:      Tomasz Pieczerak (tphaster)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "smtp.h"
#include "system.h"

/** Constants **/
#define FNMAXLEN    32  /* filename maximum length */
#define MAILBUF      5  /* mail buffer size */
#define CMDMAXLEN  512  /* command maximum lenght */

int smime_process_mails (struct mail_object **mails, char **fns, int no_mails);
char *strcasestr(const char *haystack, const char *needle);


/* smime_gate_service - receive mails from client, process them and *
 *                      send to mail server <forward-path>          */
void smime_gate_service (int sockfd)
{
    int i, srvfd;
    int srv = SMTP_SRV_NEW;
    int no_mails = 0;
    char **fns = Calloc(MAILBUF, sizeof(char *));
    char *filename = Malloc(FNMAXLEN);
    struct mail_object **mails = Calloc(MAILBUF, sizeof(struct mail_object *));
    struct mail_object *mail = Malloc(sizeof(struct mail_object));

    /* TODO: generate filename */

    while (0 == smtp_recv_mail(sockfd, mail, filename, srv)) {
        mails[no_mails] = mail;
        fns[no_mails] = filename;
        ++no_mails;

        if (NULL == (filename = malloc(FNMAXLEN))) {
            srv = SMTP_SRV_ERR;
            filename = NULL;
            mail = NULL;
            continue;
        }

        if (NULL == (mail = malloc(sizeof(struct mail_object)))) {
            srv = SMTP_SRV_ERR;
            free(filename);
            filename = NULL;
            mail = NULL;
            continue;
        }

        if (MAILBUF == no_mails)
            /* TODO: extending mail buffer */
            srv = SMTP_SRV_ERR;
        else
            srv = SMTP_SRV_NXT;
    }

    if (0 == no_mails)
        return;     /* no mails to process */

    smime_process_mails(mails, fns, no_mails);

    /* forward all received mail objects */
    srvfd = Socket(AF_INET, SOCK_STREAM, 0);
    Connect(srvfd, (SA *) &(conf.mail_srv), sizeof(conf.mail_srv));
    srv = SMTP_SRV_NEW;

    for (i = 0; i < no_mails; ++i) {
        if (0 == smtp_send_mail(srvfd, mails[i], srv))
            remove(fns[i]);

        srv = SMTP_SRV_NXT;
    }
}

int smime_process_mails (struct mail_object **mails, char **fns, int no_mails)
{
    int m, toprcs;
    unsigned int r;
    char cmd[CMDMAXLEN];

    for (m = 0; m < no_mails; ++m) {

        /** signing rules **/
        toprcs = 0;
        for (r = 0; r < conf.sign_rules_size; ++r) {
            if (NULL != conf.sign_rules[r].sndr) {
                if (strcasestr(mails[m]->mail_from, conf.sign_rules[r].sndr)) {
                    toprcs = 1;
                    break;
                }
            }
        }
        /* did we find matching rule? */
        if (toprcs) {
            snprintf(cmd, CMDMAXLEN,
                "smime-tool -sign -cert %s -key %s -pass %s %s > %s.prcs",
                conf.sign_rules[r].cert_path, conf.sign_rules[r].key_path,
                conf.sign_rules[r].key_path, fns[m], fns[m]);

            if (0 == system(cmd)) {
                snprintf(cmd, CMDMAXLEN, "%s.prcs", fns[m]);
                if (-1 == rename(cmd, fns[m]))
                    remove(cmd);
                else {
                    free_mail_object(mails[m]);
                    load_mail_from_file(fns[m], mails[m]);
                    /* signing successful */
                }
            }
        }
        /** end of signing rules **/

        /** encryption rules **/
        toprcs = 0;
        for (r = 0; r < conf.encr_rules_size; ++r) {
            if (NULL != conf.encr_rules[r].rcpt) {
                /* when there is only one recipient, encryption makes sense */
                if (1 == mails[m]->no_rcpt) {
                    if (strcasestr(mails[m]->rcpt_to[1],
                                conf.encr_rules[r].rcpt))
                    {
                        toprcs = 1;
                        break;
                    }
                }
            }
        }
        /* did we find matching rule? */
        if (toprcs) {
            snprintf(cmd, CMDMAXLEN,
                    "smime-tool -encrypt -cert %s %s > %s.prcs",
                    conf.encr_rules[r].cert_path, fns[m], fns[m]);

            if (0 == system(cmd)) {
                snprintf(cmd, CMDMAXLEN, "%s.prcs", fns[m]);
                if (-1 == rename(cmd, fns[m]))
                    remove(cmd);
                else {
                    free_mail_object(mails[m]);
                    load_mail_from_file(fns[m], mails[m]);
                    /* encryption successful */
                }
            }
        }
        else
            continue;
        /** end of encryption rules **/

        /** decryption rules **/
        toprcs = 0;
        for (r = 0; r < conf.decr_rules_size; ++r) {
            if (NULL != conf.decr_rules[r].rcpt) {
                /* when there is only one recipient, decryption makes sense */
                if (1 == mails[m]->no_rcpt) {
                    if (strcasestr(mails[m]->rcpt_to[1],
                                conf.decr_rules[r].rcpt))
                    {
                        toprcs = 1;
                        break;
                    }
                }
            }
        }
        /* did we find matching rule? */
        if (toprcs) {
            snprintf(cmd, CMDMAXLEN,
                "smime-tool -decrypt -cert %s -key %s -pass %s %s > %s.prcs",
                conf.decr_rules[r].cert_path, conf.decr_rules[r].key_path,
                conf.decr_rules[r].key_path, fns[m], fns[m]);

            if (0 == system(cmd)) {
                snprintf(cmd, CMDMAXLEN, "%s.prcs", fns[m]);
                if (-1 == rename(cmd, fns[m]))
                    remove(cmd);
                else {
                    free_mail_object(mails[m]);
                    load_mail_from_file(fns[m], mails[m]);
                    /* decryption successful */
                }
            }
        }
        /** end of decryption rules **/

        /** verification rules **/
        toprcs = 0;
        for (r = 0; r < conf.vrfy_rules_size; ++r) {
            if (NULL != conf.vrfy_rules[r].sndr) {
                if (strcasestr(mails[m]->mail_from, conf.vrfy_rules[r].sndr)) {
                    toprcs = 1;
                    break;
                }
            }
        }
        /* did we find matching rule? */
        if (toprcs) {
            snprintf(cmd, CMDMAXLEN,
                "smime-tool -verify -cert %s -ca %s %s > %s.prcs",
                conf.vrfy_rules[r].cert_path, conf.vrfy_rules[r].cacert_path,
                fns[m], fns[m]);

            if (0 == system(cmd)) {
                snprintf(cmd, CMDMAXLEN, "%s.prcs", fns[m]);
                if (-1 == rename(cmd, fns[m]))
                    remove(cmd);
                else {
                    free_mail_object(mails[m]);
                    load_mail_from_file(fns[m], mails[m]);
                    /* verification successful */
                }
            }
        }
        /** end of verification rules **/

    }

    return 0;
}

