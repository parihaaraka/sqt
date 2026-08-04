-- ============================================================================
-- Снимок дерева блокировок PostgreSQL.
--
-- Требует PostgreSQL 14+ (использует pg_locks.waitstart).
-- pg_blocking_pids() не рекомендуется дёргать часто / в постоянном
-- мониторинге: https://www.postgresql.org/docs/current/functions-info.html
--
-- Идея и базовая структура: postgres.ai (Nikolay Samokhvalov, 2021)
--   https://postgres.ai/blog/20211018-postgresql-lock-trees
-- Доработки:
--   - idle-сессии с session-level advisory lock не выпадают из выборки
--     (advisory lock не привязан к транзакции);
--   - вместо classid/objid — восстановленный исходный ключ advisory lock
--     (имён у advisory locks в принципе нет, есть только числовой ключ,
--     который выбрал разработчик приложения — см. objsubid: 1 = ключ
--     передавали как один bigint, 2 = как два int);
--   - waiting_locktype/waiting_object/waiting_mode, application_name,
--     client_addr, path — для анализа в отрыве от живого сервера.
--
-- Как читать blocked_by и path (пример):
--   pid=641449  blocked_by={}                 <- никого не ждёт, сам держит лок (корень)
--   pid=641586  blocked_by={641449}            <- ждёт напрямую 641449
--   pid=641594  blocked_by={641586,641449}     <- ждёт и 641586, и (транзитивно) 641449
--   pid=641588  blocked_by={641594}
--   pid=641590  blocked_by={641594}
--
--   blocked_by      — "плоский" список pid'ов, из-за которых ЭТОТ pid сейчас
--                      стоит (результат pg_blocking_pids()); порядок не имеет
--                      значения, это множество, а не цепочка.
--   path            — УПОРЯДОЧЕННЫЙ маршрут от корня блокировки до этой
--                      строки, как его прошёл алгоритм построения дерева:
--                        641449 -> path = {641449}
--                        641586 -> path = {641449,641586}
--                        641594 -> path = {641449,641586,641594}
--                        641588 -> path = {641449,641586,641594,641588}
--                        641590 -> path = {641449,641586,641594,641590}
--                      т.е. path[1] всегда pid корневого блокера, а
--                      последний элемент — pid текущей строки.
--
--   Обратите внимание: 641588 и 641590 оба ждут 641594, и оба получат
--   ОТДЕЛЬНУЮ строку с разными path (совпадают все элементы кроме
--   последнего) — это нормально, разные ветви одного дерева.
--   Именно поэтому path в PK: один и тот же pid в рамках одного снимка
--   (одного ts) может встретиться больше одного раза, если алгоритм
--   находит его через несколько разных цепочек-предков; уникальна не
--   сессия сама по себе, а конкретный путь до неё в дереве.
--
--   Почему pid+path НЕ дублируются даже если одна сессия удерживает
--   много разных блокировок, которые ждёт другая сессия: pg_blocking_pids()
--   возвращает МНОЖЕСТВО блокирующих pid'ов (без повторов), а не по записи
--   на каждый лок; кроме того, один бэкенд физически ждёт только ОДНУ
--   блокировку одновременно. Строка может повториться по pid только через
--   разные ветви дерева — а у разных ветвей path всегда отличается хотя бы
--   в одной позиции (либо разный корень path[1], либо разная история
--   внутри одной ветки), т.к. path только дописывается, не перезаписывается.
--
--   Известное ограничение (не дубли, а пропуск): если сессия одновременно
--   заблокирована pid'ами из двух НЕ связанных между собой корневых цепочек
--   (blocked_by = {root_A, root_B} с разными независимыми корнями), она в
--   дерево не попадёт вовсе — слияние истории (all_blockers_above)
--   происходит только внутри одной ветки начиная со 2-го уровня, а не между
--   разными корнями 1-го уровня. На практике редкость, но если в
--   pg_stat_activity видна явно заблокированная сессия, которой нет в
--   дереве, — вероятная причина именно в этом.
-- ============================================================================

-- set statement_timeout = '1s';

with recursive activity as (
    select
        pg_blocking_pids(a.pid) as blocked_by,
        a.*,
        wl.locktype as waiting_locktype,
        wl.mode     as waiting_mode,
        coalesce(
            wl.relation::regclass::text,
            case
                -- objsubid=1 -> ключ передавали как pg_advisory_lock(bigint)
                when wl.locktype = 'advisory' and wl.objsubid = 1
                    then 'advisory:' || ((wl.classid::bigint << 32) | wl.objid::bigint)::text
                -- objsubid=2 -> ключ передавали как pg_advisory_lock(int, int)
                when wl.locktype = 'advisory' and wl.objsubid = 2
                    then 'advisory:' || wl.classid || '/' || wl.objid
                when wl.locktype is not null
                    then wl.locktype::text
            end
        ) as waiting_object,
        age(now(), a.xact_start)::interval(0) as tx_age,
        age(now(), (select max(l.waitstart) from pg_locks l where a.pid = l.pid))::interval(0) as wait_age
    from pg_stat_activity a
    -- у бэкенда в любой момент максимум один неудовлетворённый запрос
    -- на блокировку; limit 1 — строгая гарантия не более 1 строки
    left join lateral (
        select locktype, mode, relation, classid, objid, objsubid
        from pg_locks
        where pid = a.pid and not granted
        limit 1
    ) wl on true
    where
        a.state is distinct from 'idle'
        or a.pid in (
            select pid from pg_locks where locktype = 'advisory' and granted
        )
), blockers as (
    select
        array_agg(distinct c order by c) as pids
    from (
        select unnest(blocked_by)
        from activity
    ) as dt(c)
), tree as (
    select
        activity.*,
        1 as level,
        activity.pid as top_blocker_pid,
        array[activity.pid] as path,
        array[activity.pid]::int[] as all_blockers_above
    from activity, blockers
    where
        array[pid] <@ blockers.pids
        and blocked_by = '{}'::int[]
    union all
    select
        activity.*,
        tree.level + 1 as level,
        tree.top_blocker_pid,
        path || array[activity.pid] as path,
        tree.all_blockers_above || array_agg(activity.pid) over () as all_blockers_above
    from activity, tree
    where
        not array[activity.pid] <@ tree.all_blockers_above
        and activity.blocked_by <> '{}'::int[]
        and activity.blocked_by <@ tree.all_blockers_above
)
select
    now() as ts,
    pid,
    path,
    blocked_by,
    case when wait_event_type <> 'Lock' then replace(state, 'idle in transaction', 'idletx') else 'waiting' end as state,
    wait_event_type || ':' || wait_event as wait,
    waiting_locktype,
    waiting_object,
    waiting_mode,
    wait_age,
    tx_age,
    to_char(age(backend_xid), 'FM999,999,999,990') as xid_age,
    to_char(2147483647 - age(backend_xmin), 'FM999,999,999,990') as xmin_ttf,
    datname,
    usename,
    application_name,
    client_addr::text as client_addr,
    (select count(distinct t1.pid) from tree t1 where array[tree.pid] <@ t1.path and t1.pid <> tree.pid) as blkd,
    format(
        '%s %s%s',
        lpad('[' || pid::text || ']', 9, ' '),
        repeat('.', level - 1) || case when level > 1 then ' ' end,
        left(query, 1000)
    ) as query
from tree
order by top_blocker_pid, level, pid
