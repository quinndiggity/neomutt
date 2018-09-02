/**
 * @file
 * Account object used by POP and IMAP
 *
 * @authors
 * Copyright (C) 2000-2007 Brendan Cully <brendan@kublai.com>
 *
 * @copyright
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @page mutt_account Account object used by POP and IMAP
 *
 * Account object used by POP and IMAP
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include "mutt/mutt.h"
#include "email/email.h"
#include "conn/conn.h"
#include "mutt_account.h"
#include "curs_lib.h"
#include "filter.h"
#include "globals.h"
#include "options.h"
#include "pop/pop.h"

/* These Config Variables are only used in mutt_account.c */
char *ImapLogin; ///< Config: (imap) Login name for the IMAP server (defaults to ImapUser)
char *ImapOauthRefreshCmd;
char *ImapPass; ///< Config: (imap) Password for the IMAP server
char *NntpPass; ///< Config: (nntp) Password for the news server
char *NntpUser; ///< Config: (nntp) Username for the news server
char *PopPass;  ///< Config: (pop) Password of the POP server
char *PopUser;  ///< Config: (pop) Username of the POP server
char *SmtpPass; ///< Config: (smtp) Password for the SMTP server
char *SmtpOauthRefreshCmd;

/**
 * mutt_account_match - Compare account info (host/port/user)
 * @param a1 First Account
 * @param a2 Second Account
 * @retval 1 Accounts match
 * @retval 0 Accounts match
 */
int mutt_account_match(const struct Account *a1, const struct Account *a2)
{
  const char *user = NONULL(Username);

  if (a1->type != a2->type)
    return 0;
  if (mutt_str_strcasecmp(a1->host, a2->host) != 0)
    return 0;
  if (a1->port != a2->port)
    return 0;

#ifdef USE_IMAP
  if (a1->type == MUTT_ACCT_TYPE_IMAP)
  {
    if (ImapUser)
      user = ImapUser;
  }
#endif

#ifdef USE_POP
  if (a1->type == MUTT_ACCT_TYPE_POP && PopUser)
    user = PopUser;
#endif

#ifdef USE_NNTP
  if (a1->type == MUTT_ACCT_TYPE_NNTP && NntpUser)
    user = NntpUser;
#endif

  if (a1->flags & a2->flags & MUTT_ACCT_USER)
    return strcmp(a1->user, a2->user) == 0;
#ifdef USE_NNTP
  if (a1->type == MUTT_ACCT_TYPE_NNTP)
    return a1->flags & MUTT_ACCT_USER && a1->user[0] ? 0 : 1;
#endif
  if (a1->flags & MUTT_ACCT_USER)
    return strcmp(a1->user, user) == 0;
  if (a2->flags & MUTT_ACCT_USER)
    return strcmp(a2->user, user) == 0;

  return 1;
}

/**
 * mutt_account_fromurl - Fill Account with information from url
 * @param account Account to fill
 * @param url     Url to parse
 * @retval  0 Success
 * @retval -1 Error
 */
int mutt_account_fromurl(struct Account *account, struct Url *url)
{
  /* must be present */
  if (url->host)
    mutt_str_strfcpy(account->host, url->host, sizeof(account->host));
  else
    return -1;

  if (url->user)
  {
    mutt_str_strfcpy(account->user, url->user, sizeof(account->user));
    account->flags |= MUTT_ACCT_USER;
  }
  if (url->pass)
  {
    mutt_str_strfcpy(account->pass, url->pass, sizeof(account->pass));
    account->flags |= MUTT_ACCT_PASS;
  }
  if (url->port)
  {
    account->port = url->port;
    account->flags |= MUTT_ACCT_PORT;
  }

  return 0;
}

/**
 * mutt_account_tourl - Fill URL with info from account
 * @param account Source Account
 * @param url     Url to fill
 *
 * The URL information is a set of pointers into account - don't free or edit
 * account until you've finished with url (make a copy of account if you need
 * it for a while).
 */
void mutt_account_tourl(struct Account *account, struct Url *url)
{
  url->scheme = U_UNKNOWN;
  url->user = NULL;
  url->pass = NULL;
  url->port = 0;
  url->path = NULL;

#ifdef USE_IMAP
  if (account->type == MUTT_ACCT_TYPE_IMAP)
  {
    if (account->flags & MUTT_ACCT_SSL)
      url->scheme = U_IMAPS;
    else
      url->scheme = U_IMAP;
  }
#endif

#ifdef USE_POP
  if (account->type == MUTT_ACCT_TYPE_POP)
  {
    if (account->flags & MUTT_ACCT_SSL)
      url->scheme = U_POPS;
    else
      url->scheme = U_POP;
  }
#endif

#ifdef USE_SMTP
  if (account->type == MUTT_ACCT_TYPE_SMTP)
  {
    if (account->flags & MUTT_ACCT_SSL)
      url->scheme = U_SMTPS;
    else
      url->scheme = U_SMTP;
  }
#endif

#ifdef USE_NNTP
  if (account->type == MUTT_ACCT_TYPE_NNTP)
  {
    if (account->flags & MUTT_ACCT_SSL)
      url->scheme = U_NNTPS;
    else
      url->scheme = U_NNTP;
  }
#endif

  url->host = account->host;
  if (account->flags & MUTT_ACCT_PORT)
    url->port = account->port;
  if (account->flags & MUTT_ACCT_USER)
    url->user = account->user;
  if (account->flags & MUTT_ACCT_PASS)
    url->pass = account->pass;
}

/**
 * mutt_account_getuser - Retrieve username into Account, if necessary
 * @param account Account to fill
 * @retval  0 Success
 * @retval -1 Failure
 */
int mutt_account_getuser(struct Account *account)
{
  char prompt[STRING];

  /* already set */
  if (account->flags & MUTT_ACCT_USER)
    return 0;
#ifdef USE_IMAP
  else if ((account->type == MUTT_ACCT_TYPE_IMAP) && ImapUser)
    mutt_str_strfcpy(account->user, ImapUser, sizeof(account->user));
#endif
#ifdef USE_POP
  else if ((account->type == MUTT_ACCT_TYPE_POP) && PopUser)
    mutt_str_strfcpy(account->user, PopUser, sizeof(account->user));
#endif
#ifdef USE_NNTP
  else if ((account->type == MUTT_ACCT_TYPE_NNTP) && NntpUser)
    mutt_str_strfcpy(account->user, NntpUser, sizeof(account->user));
#endif
  else if (OptNoCurses)
    return -1;
  /* prompt (defaults to unix username), copy into account->user */
  else
  {
    /* L10N: Example: Username at myhost.com */
    snprintf(prompt, sizeof(prompt), _("Username at %s: "), account->host);
    mutt_str_strfcpy(account->user, Username, sizeof(account->user));
    if (mutt_get_field_unbuffered(prompt, account->user, sizeof(account->user), 0))
      return -1;
  }

  account->flags |= MUTT_ACCT_USER;

  return 0;
}

/**
 * mutt_account_getlogin - Retrieve login info into Account, if necessary
 * @param account Account to fill
 * @retval  0 Success
 * @retval -1 Failure
 */
int mutt_account_getlogin(struct Account *account)
{
  /* already set */
  if (account->flags & MUTT_ACCT_LOGIN)
    return 0;
#ifdef USE_IMAP
  else if (account->type == MUTT_ACCT_TYPE_IMAP)
  {
    if (ImapLogin)
    {
      mutt_str_strfcpy(account->login, ImapLogin, sizeof(account->login));
      account->flags |= MUTT_ACCT_LOGIN;
    }
  }
#endif

  if (!(account->flags & MUTT_ACCT_LOGIN))
  {
    if (mutt_account_getuser(account) == 0)
    {
      mutt_str_strfcpy(account->login, account->user, sizeof(account->login));
      account->flags |= MUTT_ACCT_LOGIN;
    }
    else
    {
      mutt_debug(1, "Couldn't get user info\n");
      return -1;
    }
  }

  return 0;
}

/**
 * mutt_account_getpass - Fetch password into Account, if necessary
 * @param account Account to fill
 * @retval  0 Success
 * @retval -1 Failure
 */
int mutt_account_getpass(struct Account *account)
{
  char prompt[STRING];

  if (account->flags & MUTT_ACCT_PASS)
    return 0;
#ifdef USE_IMAP
  else if ((account->type == MUTT_ACCT_TYPE_IMAP) && ImapPass)
    mutt_str_strfcpy(account->pass, ImapPass, sizeof(account->pass));
#endif
#ifdef USE_POP
  else if ((account->type == MUTT_ACCT_TYPE_POP) && PopPass)
    mutt_str_strfcpy(account->pass, PopPass, sizeof(account->pass));
#endif
#ifdef USE_SMTP
  else if ((account->type == MUTT_ACCT_TYPE_SMTP) && SmtpPass)
    mutt_str_strfcpy(account->pass, SmtpPass, sizeof(account->pass));
#endif
#ifdef USE_NNTP
  else if ((account->type == MUTT_ACCT_TYPE_NNTP) && NntpPass)
    mutt_str_strfcpy(account->pass, NntpPass, sizeof(account->pass));
#endif
  else if (OptNoCurses)
    return -1;
  else
  {
    snprintf(prompt, sizeof(prompt), _("Password for %s@%s: "),
             (account->flags & MUTT_ACCT_LOGIN) ? account->login : account->user,
             account->host);
    account->pass[0] = '\0';
    if (mutt_get_password(prompt, account->pass, sizeof(account->pass)))
      return -1;
  }

  account->flags |= MUTT_ACCT_PASS;

  return 0;
}

/**
 * mutt_account_unsetpass - Unset Account's password
 * @param account Account to modify
 */
void mutt_account_unsetpass(struct Account *account)
{
  account->flags &= ~MUTT_ACCT_PASS;
}

/* mutt_account_getoauthbearer: call external command to generate the
 * oauth refresh token for this ACCOUNT, then create and encode the
 * OAUTHBEARER token based on RFC 7628.  Returns NULL on failure.
 * Resulting token is dynamically allocated and should be FREE'd by the
 * caller.
 */
char *mutt_account_getoauthbearer(struct Account *account)
{
  FILE *fp;
  char *cmd = NULL;
  char *token = NULL;
  size_t token_size = 0;
  char *oauthbearer = NULL;
  size_t oalen;
  char *encoded_token = NULL;
  size_t encoded_len;
  pid_t pid;

  /* The oauthbearer token includes the login */
  if (mutt_account_getlogin(account))
    return NULL;

#ifdef USE_IMAP
  if ((account->type == MUTT_ACCT_TYPE_IMAP) && ImapOauthRefreshCmd)
    cmd = ImapOauthRefreshCmd;
#endif
#ifdef USE_POP
  else if ((account->type == MUTT_ACCT_TYPE_POP) && PopOauthRefreshCmd)
    cmd = PopOauthRefreshCmd;
#endif
#ifdef USE_SMTP
  else if ((account->type == MUTT_ACCT_TYPE_SMTP) && SmtpOauthRefreshCmd)
    cmd = SmtpOauthRefreshCmd;
#endif

  if (cmd == NULL)
  {
    mutt_error(
        _("mutt_account_getoauthbearer: No OAUTH refresh command defined"));
    return NULL;
  }

  if ((pid = mutt_create_filter(cmd, NULL, &fp, NULL)) < 0)
  {
    mutt_perror(
        _("mutt_account_getoauthbearer: Unable to run refresh command"));
    return NULL;
  }

  /* read line */
  token = mutt_file_read_line(NULL, &token_size, fp, NULL, 0);
  mutt_file_fclose(&fp);
  mutt_wait_filter(pid);

  if (token == NULL || *token == '\0')
  {
    mutt_error(_("mutt_account_getoauthbearer: Command returned empty string"));
    FREE(&token);
    return NULL;
  }

  /* Determine the length of the keyed message digest, add 50 for
   * overhead.
   */
  oalen = strlen(account->login) + strlen(account->host) + strlen(token) + 50;
  oauthbearer = mutt_mem_malloc(oalen);

  snprintf(oauthbearer, oalen, "n,a=%s,\001host=%s\001port=%d\001auth=Bearer %s\001\001",
           account->login, account->host, account->port, token);

  FREE(&token);

  encoded_len = strlen(oauthbearer) * 4 / 3 + 10;
  encoded_token = mutt_mem_malloc(encoded_len);
  mutt_b64_encode(oauthbearer, strlen(oauthbearer), encoded_token, encoded_len);
  FREE(&oauthbearer);
  return encoded_token;
}
