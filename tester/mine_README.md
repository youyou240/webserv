# Testeurs maison (`mine_*`)

Nos propres testeurs pour valider **webserv**, en complément des binaires
officiels de 42 (`tester`, `cgi_tester`). Le préfixe `mine_` les distingue des
outils fournis par l'école.

Écrits en **Python 3 (stdlib uniquement)** : aucun `pip`, aucune dépendance.
Ils parlent HTTP sur des **sockets bruts**, ce qui permet d'envoyer des requêtes
volontairement malformées (chunked, slowloris, octets poubelle) qu'un client
« propre » comme `curl` refuse de produire.

> ⚠️ À lancer **dans le container Docker** (là où tourne webserv). Tout se fait
> pendant que **toi** tu lances le serveur — les scripts ne compilent ni ne
> démarrent jamais webserv (sauf le rappel de commande dans `mine_leakcheck.sh`).

## Prérequis

1. webserv compilé et lancé avec la config d'exemple :
   ```
   ./webserv configs/default.conf
   ```
   (le serveur écoute alors sur `:8080` — localhost + site-b.local — et `:9090`)
2. `python3` disponible (présent dans le container).

## Les fichiers

| Fichier | Ce qu'il teste |
|---|---|
| `mine_http.py` | Bibliothèque partagée (client HTTP raw + reporter). Pas exécuté seul. |
| `mine_functional.py` | **Correctness** : codes de statut, headers, corps de chaque feature. |
| `mine_robustness.py` | **Résistance** aux entrées cassées + serveur reste disponible. |
| `mine_security.py` | **Sécurité** : rejoue chaque défense de `docs/SECURITY.md`. |
| `mine_cgi_stress.py` | **CGI en concurrence** + cas tordus (timeout, deadlock, cross-talk). |
| `mine_fuzz.py` | **Fuzzing** du parseur : le serveur survit-il à des requêtes aléatoires ? |
| `mine_stress.py` | **Charge** concurrente + FD + serveur toujours vivant. |
| `mine_leakcheck.sh` | Wrapper **valgrind** (fuites mémoire + FD) pendant la charge. |

## Lancer

Rends les scripts exécutables une fois :
```
chmod +x tester/mine_*.py tester/mine_leakcheck.sh
```

Puis, serveur démarré dans un autre terminal :

```
# Fonctionnel (rapide)
./tester/mine_functional.py http://localhost:8080

# Robustesse (~30s à cause du test slowloris ; --no-slow pour le sauter)
./tester/mine_robustness.py http://localhost:8080
./tester/mine_robustness.py http://localhost:8080 --no-slow

# Sécurité (rejoue les attaques de docs/SECURITY.md)
./tester/mine_security.py http://localhost:8080

# CGI en concurrence + cas limites (idéalement sous valgrind)
./tester/mine_cgi_stress.py http://localhost:8080 --workers 30 --duration 8

# Fuzzing du parseur (reproductible via --seed ; idéalement sous valgrind)
./tester/mine_fuzz.py http://localhost:8080 --iterations 5000 --seed 42

# Stress (paramétrable)
./tester/mine_stress.py http://localhost:8080 --workers 50 --duration 10

# Fuites mémoire + FD (interactif, guide valgrind)
./tester/mine_leakcheck.sh http://localhost:8080
```

Chaque script affiche des lignes `[PASS]` / `[FAIL]` / `[SKIP]` et sort avec le
code **0** si tout est vert (pratique pour enchaîner :
`./mine_functional.py ... && ./mine_stress.py ...`).

## Ce que couvre `mine_functional.py`

| Test | Attendu |
|---|---|
| `GET /`, `/style.css`, `/about.html` | 200 + bon type MIME |
| `Content-Length` | == taille réelle du corps |
| `GET /nope.html` | 404 + page d'erreur **custom** |
| `GET /private/` | 403 (autoindex off, pas d'index) |
| `POST /`, `DELETE /` | 405 + header `Allow: GET` |
| `GET /old` | 301 + `Location` |
| `GET /uploads/` | 200 (autoindex on) |
| POST upload (brut) → GET → DELETE | 201 → 200 → 204 → 404 |
| POST upload `multipart/form-data` | 201 + fichier relisible |
| POST **chunked** | 201 + corps dé-chunké intact |
| POST corps > `client_max_body_size` | 413 |
| `Host: site-b.local` | sert le vhost `www-b` |
| CGI GET/POST (`.py`, `.cgi`, `.php`) | 200 (`.php` **SKIP** si `php-cgi` absent) |
| `Expect: 100-continue` | 100 puis 201 |
| Keep-alive | 2 requêtes / même socket → 200 + 200 |
| `GET :9090/` | 200 (autoindex on) |

## Ce que couvre `mine_robustness.py`

Entrées malformées → bon code d'erreur **et** serveur toujours vivant après :
octets poubelle (400), request-line incomplète (400), méthode inconnue (501),
`HTTP/2.0` (505), URI géante (414), headers gigantesques (400/431), requête
partielle + close, half-close (`SHUT_WR`), tempête de 300 connexions, slowloris
(le serveur doit **timeout** la connexion lente sans cesser de servir les autres).

## Ce que couvre `mine_security.py`

Rejoue une attaque par défense de `docs/SECURITY.md` (chaque test est étiqueté
`[§X.Y]`) et vérifie que le serveur la neutralise **et** reste vivant :

| Test | Section | Attendu |
|---|---|---|
| `Host` absent en HTTP/1.1 | §2.6 | 400 |
| CRLF encodé (`%0d%0a`), NUL (`%00`), `%zz` | §2.3 | 400 |
| Clé de header avec espace, ctl char en valeur | §2.4 | 400 |
| `Host` / `Content-Length` en double, `CL`+`TE` | §2.5 | 400 |
| `Content-Length` en overflow (2^64) | §2.7 | 400 |
| Chunk hexa invalide, CRLF de chunk manquant | §2.8 | 400 |
| Bloc de headers géant | §2.9 | 400/431 |
| Path traversal brut / encodé / double-encodé | §3.1 | pas de fuite `/etc/passwd` |
| Dossier privé (autoindex off) | §3.5 | 403 |
| Upload `filename` de traversal | §3.7 | confiné au basename dans `upload_store` |
| POST avec URL de traversal | §3.7 / §3.1 | pas d'écriture hors store |
| Nom de fichier XSS dans l'autoindex | §4.1 | HTML-échappé (`&lt;script&gt;`) |
| `Content-Length` calculé | §4.2 | == taille réelle |
| Méthode non gérée (`PATCH`), version (`HTTP/3.0`) | §6.1 | 501 / 505 |

## Ce que couvre `mine_cgi_stress.py`

Le CGI est la zone la plus risquée (fork/pipe/execve/waitpid). Cette suite le
pousse **en concurrence** et sur ses cas limites (à lancer sous valgrind pour
les fuites de FD de pipe) :

- CGI **GET** concurrents (`.py` + `.cgi`) → fork storm, ≥ 99 % de succès.
- CGI **POST** concurrents avec **token unique** → **0 cross-talk** entre les
  pipes de deux CGI qui tournent en même temps (le test le plus fin).
- CGI qui **boucle** → **504** + kill (script `sleep` créé/supprimé à la volée).
- CGI **sans sortie** → **502**.
- CGI à **grosse sortie (5 Mo)** → 200 + corps lu jusqu'à EOF.
- **Gros POST (256 Ko)** vers un CGI qui lit stdin → pas de **deadlock**, corps relayé.

> Crée temporairement des scripts `mine_*_<pid>.py` dans `www/cgi-bin/` puis les
> supprime. Nécessite un accès en écriture à ce dossier.

## Ce que couvre `mine_fuzz.py`

Fuzzer de mutation : part de requêtes valides, les mutile aléatoirement
(byte-flip, insertion, troncature, CRLF injectés, nombres délirants, lignes
dupliquées…) et vérifie **une seule chose** : le serveur ne **crashe** ni ne
**hang** jamais. Il a le droit de répondre 400 ou de fermer — pas de mourir.

- `--iterations N` : nombre de requêtes fuzzées (défaut 5000).
- `--seed S` : rend le run **reproductible** ; si le serveur meurt, la dernière
  charge est affichée en `repr()` pour rejouer le bug.
- À lancer **sous valgrind** pour attraper en plus les corruptions mémoire.

## Ce que couvre `mine_stress.py`

- Charge concurrente soutenue (workers × durée) → **≥ 99 %** de succès.
- 1000 connexions ouvertes/fermées → éprouve les FD.
- Vérifie que le serveur **répond toujours** après le stress.

## Notes

- Les tests d'upload créent des fichiers dans `www/uploads/` puis les
  **suppriment** (via DELETE). Si un run est interrompu, quelques `mine_*.txt`
  peuvent rester : sans conséquence.
- `mine_leakcheck.sh` ne tue pas webserv lui-même (le serveur a un menu
  interactif sur `Ctrl-C`) : il te guide pour lancer valgrind, génère la charge,
  puis te dit quoi vérifier après avoir choisi `EXIT`.
