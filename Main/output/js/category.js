let params = new URLSearchParams(window.location.search);
let title = params.get("title");
let type = params.get("type");
let value = params.get("value");

let input = document.getElementById("search-input");
let button = document.getElementById("search-button");
let sortDesc = document.getElementById("sort-desc");
let sortAsc = document.getElementById("sort-asc");
let keyword = "";
let keywords = [];

let newsList = [];
let result = [];
let result_backup = [];
let title_backup = [];

//筛选新闻
function filterNews(newsList){

    result = [];

    if(type == "time"){

    let now = new Date();

    if(value == "today"){
        //筛选今日新闻
        result = newsList.filter(news=>{
            let date = new Date(news.time);
            return(
                date.getFullYear() == now.getFullYear()
                &&
                date.getMonth() == now.getMonth()
                &&
                date.getDate() == now.getDate()
            );
            })
    }

    else if(value == "weekago"){
        //筛选本周新闻
        result = newsList.filter(news=>{
            let date = new Date(news.time);
            let diff = now - date;
            let day = 1000 * 60 * 60 * 24;
            return diff <= 7 * day;
        })
    }

    else if(value == "monthago"){
        //筛选本月新闻
        result = newsList.filter(news=>{
            let date = new Date(news.time);
            return(
                date.getFullYear() == now.getFullYear()
                &&
                date.getMonth() == now.getMonth()
            );
        })

    }

    else if(value == "history"){
        //筛选历史新闻
        result = [...newsList];
    }

    }


    else if(type == "source"){

        result = newsList.filter(news=>news.source==value);

    }


    else if(type == "topic"){

        result = newsList.filter(news=>news.topic==value);

    }

    return result;

}


//展示新闻
function printNewsList(){

    if(result.length === 0){
        document.getElementById("news-list").innerHTML =
        `
        <div class="news-card">
            <div class="news-title">
                暂无相关资讯
            </div>
        </div>
        `;
        return;
    }

    let html = "";
    result.forEach(news=>{
        html +=
        `
        <div class="news-card">

            <div class="news-source">
                ${highlight(news.source, keywords)}
            </div>

            <div class="news-title">
                ${highlight(news.title, keywords)}
            </div>

            <div class="news-time">
                ${highlight(news.time, keywords)}
            </div>

            <div class="news-abstract">
                ${highlight(news.abstract, keywords)}
            </div>

            <a class="detail-btn"
            href="detail/${news.id}.html?from=category_result&title=${title}&type=${type}&value=${value}&keyword=${keyword}">

                查看详情

            </a>

        </div>
        `;
    });

    document.getElementById("news-list").innerHTML = html;

}


// 正则特殊字符转义
function escapeRegExp(string){

    return string.replace(
        /[.*+?^${}()|[\]\\]/g,
        '\\$&'
    );

}


// 关键词高亮
function highlight(text, keywords){

    if(!text){

        return "";

    }

    if(keywords.length===0){

        return text;

    }

    // 长关键词优先
    let keys = [...keywords].sort((a,b)=>b.length-a.length);  //...将keywords展开，生成一个新数组

    keys.forEach(key=>{

        if(key.trim()===""){

            return;

        }

        let reg =
        new RegExp(
            escapeRegExp(key),
            "g"
        );

        text =
        text.replace(
            reg,
            match=>
            `<span class="highlight">${match}</span>`
        );

    });

    return text;

}


//二次搜索
function searchNews(newsList){

    keyword = input.value.trim() || params.get("keyword") || "";
    keywords = keyword.trim().split(/\s+/).filter( key=>key.length>0 );

    // 空关键词显示全部
    if(keywords.length === 0){
        result = [...newsList];
    }

    else{
        result = newsList.filter(news=>{
            return keywords.every(key=>{
                return (
                    (news.title &&
                    news.title.includes(key))
                    ||
                    (news.time &&
                    news.time.includes(key))
                    ||
                    (news.abstract &&
                    news.abstract.includes(key))
                    ||
                    (news.content &&
                    news.content.includes(key))
                    ||
                    (news.source &&
                    news.source.includes(key))
                    ||
                    (news.topic &&
                    news.topic.includes(key))
                );
            });
        });
    }

    return result;

}


//初始化
fetch("json/news.json")
.then(response=>{
    if(!response.ok){
        throw new Error( "news.json加载失败" );
    }
    return response.json();  

})
.then(data=>{

    newsList = data;
    result = filterNews(newsList);
    result_backup = [...result];
    title_backup = title; 
    searchNews(result_backup);
    if(keyword){
        document.getElementById("category-title").innerHTML = "在该分类下搜索结果：" + keyword;
    }
    else{
        document.getElementById("category-title").innerHTML = title_backup;
    }
    printNewsList();


    
    button.addEventListener(
        "click",
        function(){
            result = searchNews(result_backup);
            if(keyword){
                document.getElementById("category-title").innerHTML = "在该分类下搜索结果：" + keyword;
            }
            else{
                document.getElementById("category-title").innerHTML = title_backup;
            }
            printNewsList();
        }
    );

    sortDesc.addEventListener(
        "click",
        function(){
            result.sort(
                (a,b)=>
                new Date(b.time)
                -
                new Date(a.time)
            );
            printNewsList();
        }
    );

    sortAsc.addEventListener(
        "click",
        function(){
            result.sort(
                (a,b)=>
                new Date(a.time)
                -
                new Date(b.time)
            );
            printNewsList();
        }
    );

})


.catch(error=>{
    console.error( "新闻数据加载失败:", error );
});



