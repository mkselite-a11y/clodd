# Hosting the friend website on Cloudflare

The friend site and its server are one Cloudflare Worker (`server/`). It serves the
web page and keeps one Durable Object per room, which holds the live run state,
friends' favor and the votes. This fits in Cloudflare's free plan.

## 1. Deploy the Worker (one time)

You need [Node.js](https://nodejs.org) (version 20 or newer) and a free Cloudflare account.

```sh
cd server
npm install
npx wrangler login      # opens a browser to authorize your Cloudflare account
npx wrangler deploy
```

Wrangler prints your URL at the end, for example:

```
https://moonfall-mayhem.<your-subdomain>.workers.dev
```

That's the Server URL for the mod, and the address your friends open.
To use your own domain instead, add a Custom Domain to the Worker in the Cloudflare
dashboard (Workers & Pages > moonfall-mayhem > Settings > Domains & Routes).

## 2. Point the mod at it

In MM Recomp, open **Mods**, select **Moonfall Mayhem**, and set:

| Option | Value |
|---|---|
| Friend Website | On |
| Server URL | the URL from step 1 |
| Room Code | anything your friends can type, for example `TERMINA` (letters, digits, `-`, `_`) |
| Host Key | a private password, at least 4 characters |

The first game that connects to a room claims it with its Host Key. After that, only a
game with the same key can post to that room. If you forget the key, pick a new Room Code.

In game, **Moon Menu > Friends** shows the connection status. The native library also
writes `moonfall_net.log` next to the DLL in your mods folder, which lists
connection errors.

## 3. Invite friends

Send them `https://<your worker url>/?room=TERMINA`. They pick a nickname and join.
Friends earn 1 favor every 15 seconds while you're playing (up to 20) and spend it on:

| Action | Favor | Needs "Friends can hurt you" |
|---|---|---|
| Send a message | 1 | no |
| Heal 1 heart | 3 | no |
| Moonshield (6s) | 4 | no |
| Send 5 Moon Marks | 4 | no |
| Gift a pouch item | 5 | no |
| Weaken the nemesis | 6 | no |
| Post a bounty | 6 | no |
| Rename the nemesis | 4 | no |
| Summon a harmless/benefit event | 3 | no |
| Summon a danger event | 6 | yes |
| Random event | 4 | yes |
| Freeze your feet (5s) | 3 | yes |
| Empower the nemesis | 8 | yes |
| Send the nemesis hunting | 7 | yes |
| Crown a nemesis | 10 | yes |
| Reroll the curse | 12 | yes |

Every 4 minutes the Court also gets a free vote between three events; the winner
triggers when the vote closes.

You stay in control from **Moon Menu > Settings**: turn friend meddling off entirely,
allow only helpful actions ("Friends can hurt you: No"), or change the per-friend
cooldown (20s by default). Friend actions only land during normal gameplay, never
in cutscenes or menus.

## Updating

After pulling changes, run `npx wrangler deploy` again from `server/`.

## Running locally (optional)

```sh
cd server
npx wrangler dev
```

Then set the mod's Server URL to `http://127.0.0.1:8787`.

## Costs

The game posts once per second while you play and friends use WebSockets, so a long
session makes a few thousand requests. The free plan allows 100,000 requests a day.
